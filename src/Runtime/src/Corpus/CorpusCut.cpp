#include "CorpusCutStorage.hpp"
#include "BinaryMarketBlockCodec.hpp"
#include "Codec/Crc32c.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusReader.hpp"
#include "hftrec/CorpusContract/BinaryMarketDepthTransaction.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include "cxet/Primitives/Buf/AsmPrimitives.hpp"
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <utility>
#include <unistd.h>
#include <sys/stat.h>

namespace hftrec::corpus {
namespace {
struct CutHeader final {
    std::uint64_t magic{0x4355544852434f52ull};
    std::uint64_t bytes{},generation{},committedMonotonicNs{};
    CorpusProducerProgress progress{};
    BinaryMarketManifest manifest{};
    std::uint64_t sources{},gaps{},blocks{},referenceBytes{};
    std::uint32_t crc{},headerCrc{},sourcesCrc{};
    std::uint8_t finalized{};
    std::uint8_t reserved{};
};
bool ownedCorpusFile(const std::filesystem::path& path) noexcept {
    struct stat state{};
    return ::lstat(path.c_str(),&state)==0 && S_ISREG(state.st_mode) && state.st_uid==::geteuid();
}
std::filesystem::path blockPath(const std::filesystem::path& root,const CorpusBlockRange& range,const char* suffix) {
    char name[96]{};
    (void)std::snprintf(name,sizeof(name),"shard-%04u-segment-%06u.%s",
        static_cast<unsigned>(range.shardIndex),static_cast<unsigned>(range.segmentNumber),suffix);
    return root/"segments"/name;
}
std::uint32_t crc(std::span<const std::uint8_t> data) noexcept {
    return codec::crc32c(data.data(),data.size());
}
std::uint32_t cutHeaderCrc(const CutHeader& header) noexcept {
    std::array<std::uint8_t,sizeof(CutHeader)> bytes{};
    cxet::bytescan::hftMemcpy(bytes.data(),&header,sizeof(header));
    std::fill_n(bytes.begin()+offsetof(CutHeader,crc),sizeof(header.crc),0u);
    std::fill_n(bytes.begin()+offsetof(CutHeader,headerCrc),sizeof(header.headerCrc),0u);
    return crc(bytes);
}
bool validCutHeader(const CutHeader& header,std::uint64_t bytes) noexcept {
    const auto maximum=header.manifest.maximumBytes;
    if(header.magic!=CutHeader{}.magic ||
       header.headerCrc!=cutHeaderCrc(header) || header.bytes!=bytes || bytes<sizeof(header) || !maximum || bytes>maximum ||
       !header.generation || !header.committedMonotonicNs || !header.sources || header.sources>kBinaryMarketMaximumSources ||
       header.sources!=header.manifest.sourceCount || header.gaps>header.sources*kBinaryMarketStoredChannelCount ||
       header.blocks>maximum/sizeof(CorpusBlockRange) || header.referenceBytes>kCorpusMaximumReferenceBytes ||
       header.finalized>1 || (header.finalized!=0)!=(header.manifest.finalizedReceiveNs>0) ||
       header.reserved || header.manifest.magic!=kBinaryMarketManifestMagic ||
       header.manifest.headerBytes!=sizeof(BinaryMarketManifest) ||
       !header.manifest.producerEpoch ||
       header.manifest.startedReceiveNs<=0 || !header.manifest.replayAnchorMonotonicNs ||
       header.progress.producerEpoch!=header.manifest.producerEpoch || !header.progress.shardCount ||
       header.progress.shardCount!=header.manifest.shardCount || header.progress.shardCount>kBinaryMarketMaximumShards ||
       header.gaps!=header.manifest.gapCount) return false;
    std::uint64_t remaining=bytes-sizeof(header);
    const std::array<std::pair<std::uint64_t,std::uint64_t>,4> sections{{
        {header.sources,sizeof(BinaryMarketSource)},{header.gaps,sizeof(BinaryMarketGap)},
        {header.blocks,sizeof(CorpusBlockRange)},{header.referenceBytes,1u}}};
    for(const auto& [count,size]:sections) {
        if(count>remaining/size)return false;
        remaining-=count*size;
    }
    return remaining==0;
}
bool readCutHeader(std::ifstream& file,CutHeader& header,std::string& error) {
    const auto bytes=file.tellg();file.seekg(0);file.read(reinterpret_cast<char*>(&header),sizeof(header));
    if(!file || bytes<0 || !validCutHeader(header,static_cast<std::uint64_t>(bytes))) {
        error="corpus cut header bounds or CRC mismatch";return false;
    }
    return true;
}
template<typename T> void pack(std::vector<std::uint8_t>& bytes,std::span<const T> values) {
    if(values.empty()) return;
    const auto* first=reinterpret_cast<const std::uint8_t*>(values.data());
    bytes.insert(bytes.end(),first,first+values.size_bytes());
}
template<typename T> bool unpack(std::span<const std::uint8_t>& bytes,std::uint64_t count,std::vector<T>& out) {
    if(count>bytes.size()/sizeof(T)) return false;
    out.resize(static_cast<std::size_t>(count));
    if(count) cxet::bytescan::hftMemcpy(out.data(),bytes.data(),out.size()*sizeof(T));
    bytes=bytes.subspan(out.size()*sizeof(T));return true;
}
bool finalizedCutIsDurable(const std::filesystem::path& root,const CorpusCut& cut) {
    if(!cut.finalized || !ownedCorpusFile(root/"manifest.bin"))return false;
    // Writer creates this canonical manifest only AFTER publishCut's file and
    // directory fsync succeed. Its exact matching bytes therefore witness the
    // earlier cut durability even if Recorder has since exited or switched root.
    std::ifstream file(root/"manifest.bin",std::ios::binary|std::ios::ate);
    if(!file || file.tellg()!=static_cast<std::streamoff>(sizeof(BinaryMarketManifest)))return false;
    BinaryMarketManifest manifest{};file.seekg(0);file.read(reinterpret_cast<char*>(&manifest),sizeof(manifest));
    if(!file)return false;
    const auto stored=manifest.manifestCrc32c;manifest.manifestCrc32c=0;
    if(crc({reinterpret_cast<const std::uint8_t*>(&manifest),sizeof(manifest)})!=stored)return false;
    auto expected=cut.manifest;expected.manifestCrc32c=0;
    return cxet::bytescan::hftMemcmp(&manifest,&expected,sizeof(manifest))==0;
}
bool before(const BinaryMarketRecord& a,const BinaryMarketRecord& b) {
    const auto& x=a.header;const auto& y=b.header;
    if(x.receiveMonotonicNs!=y.receiveMonotonicNs) return x.receiveMonotonicNs<y.receiveMonotonicNs;
    if(x.shardIndex!=y.shardIndex) return x.shardIndex<y.shardIndex;
    return x.shardSequence<y.shardSequence;
}
}
std::string corpusIdentity(const BinaryMarketManifest& manifest) {
    return "cxm:"+std::to_string(manifest.producerEpoch);
}
Status readCorpusSummary(const std::filesystem::path& root,CorpusSummary& summary,std::string& error) noexcept {
    summary={};error.clear();
    if(root.empty()) return Status::InvalidArgument;
    try {
        std::ifstream cut(root/"cut.bin",std::ios::binary|std::ios::ate);
        if(cut) {
            if(!ownedCorpusFile(root/"cut.bin")) {error="corpus cut file ownership mismatch";return Status::CorruptData;}
            CutHeader header{};
            if(!readCutHeader(cut,header,error))return Status::CorruptData;
            summary.manifest=header.manifest;summary.cutGeneration=header.generation;summary.cutBytes=header.bytes;summary.finalized=header.finalized!=0;
            return Status::Ok;
        }
        // Historical sealed corpora have no active cut; their fixed manifest
        // already has a standalone CRC. Do not load a source table just to list.
        std::ifstream file(root/"manifest.bin",std::ios::binary|std::ios::ate);
        if(!file) {error="corpus summary is unavailable";return Status::IoError;}
        if(!ownedCorpusFile(root/"manifest.bin")) {error="corpus manifest file ownership mismatch";return Status::CorruptData;}
        BinaryMarketManifest manifest{};
        if(file.tellg()!=static_cast<std::streamoff>(sizeof(manifest))) {
            error="corpus manifest summary size mismatch";return Status::CorruptData;
        }
        file.seekg(0);file.read(reinterpret_cast<char*>(&manifest),sizeof(manifest));
        const auto stored=manifest.manifestCrc32c;manifest.manifestCrc32c=0;
        const auto calculated=crc({reinterpret_cast<const std::uint8_t*>(&manifest),sizeof(manifest)});
        manifest.manifestCrc32c=stored;
        if(!file || stored!=calculated || manifest.magic!=kBinaryMarketManifestMagic ||
           manifest.headerBytes!=sizeof(manifest) ||
           !manifest.producerEpoch || manifest.startedReceiveNs<=0 ||
           manifest.finalizedReceiveNs<manifest.startedReceiveNs || !manifest.maximumBytes || manifest.projectedBytes>manifest.maximumBytes ||
           !manifest.sourceCount || manifest.sourceCount>kBinaryMarketMaximumSources || !manifest.shardCount ||
           manifest.shardCount>kBinaryMarketMaximumShards || manifest.complete!=1u ||
           manifest.stopReason<BinaryMarketStopReason::Requested || manifest.stopReason>BinaryMarketStopReason::ParserDisconnected ||
           manifest.reserved!=decltype(manifest.reserved){}) {
            error="corpus manifest summary contract mismatch";return Status::CorruptData;
        }
        summary.manifest=manifest;summary.finalized=true;return Status::Ok;
    } catch(...) {error="corpus summary read failed";return Status::IoError;}
}
Status readCorpusSourceCatalog(const std::filesystem::path& root,std::vector<BinaryMarketSource>& sources,std::string& error) noexcept {
    sources.clear();error.clear();
    if(root.empty())return Status::InvalidArgument;
    try {
        std::ifstream cut(root/"cut.bin",std::ios::binary|std::ios::ate);
        if(!cut) {
            if(!ownedCorpusFile(root/"manifest.bin") || !ownedCorpusFile(root/"sources.bin")) {
                error="corpus source catalog file ownership mismatch";return Status::CorruptData;
            }
            BinaryMarketManifest manifest{};
            return BinaryMarketCorpusReader{}.catalog(root,manifest,sources,error);
        }
        if(!ownedCorpusFile(root/"cut.bin")) {error="corpus cut file ownership mismatch";return Status::CorruptData;}
        CutHeader header{};
        if(!readCutHeader(cut,header,error))return Status::CorruptData;
        sources.resize(static_cast<std::size_t>(header.sources));
        cut.read(reinterpret_cast<char*>(sources.data()),static_cast<std::streamsize>(sources.size()*sizeof(BinaryMarketSource)));
        if(!cut || crc({reinterpret_cast<const std::uint8_t*>(sources.data()),sources.size()*sizeof(BinaryMarketSource)})!=header.sourcesCrc) {
            sources.clear();error="corpus source catalog CRC mismatch";return Status::CorruptData;
        }
        for(std::size_t n=0;n<sources.size();++n) if(!validBinaryMarketSource(sources[n],n)) {
            sources.clear();error="corpus source catalog contract mismatch";return Status::CorruptData;
        }
        return Status::Ok;
    } catch(...) {sources.clear();error="corpus source catalog read failed";return Status::IoError;}
}
bool syncCorpusFile(const std::filesystem::path& path,bool directory) noexcept {
    const int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|(directory?O_DIRECTORY:0));
    if(fd<0) return false;
    int result;do {result=::fsync(fd);} while(result!=0 && errno==EINTR);
    const bool ok=result==0;::close(fd);return ok;
}
Status writeCorpusCut(const std::filesystem::path& root,const CorpusCut& cut) noexcept {
    try {
        CutHeader header{};header.generation=cut.generation;header.committedMonotonicNs=cut.committedMonotonicNs;
        header.progress=cut.progress;header.manifest=cut.manifest;header.finalized=cut.finalized?1u:0u;
        header.sources=cut.sources.size();header.gaps=cut.gaps.size();header.blocks=cut.blocks.size();
        header.referenceBytes=cut.referencePayload.size();
        std::vector<std::uint8_t> bytes(sizeof(header));
        pack(bytes,std::span<const BinaryMarketSource>(cut.sources));pack(bytes,std::span<const BinaryMarketGap>(cut.gaps));
        pack(bytes,std::span<const CorpusBlockRange>(cut.blocks));pack(bytes,std::span<const std::uint8_t>(cut.referencePayload));
        header.bytes=bytes.size();
        header.sourcesCrc=crc({reinterpret_cast<const std::uint8_t*>(cut.sources.data()),cut.sources.size()*sizeof(BinaryMarketSource)});
        header.headerCrc=cutHeaderCrc(header);cxet::bytescan::hftMemcpy(bytes.data(),&header,sizeof(header));
        header.crc=crc(bytes);cxet::bytescan::hftMemcpy(bytes.data(),&header,sizeof(header));
        const auto temporary=root/"cut.pending";
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));file.flush();
        if(!file.good()) return Status::IoError;
        file.close();if(!syncCorpusFile(temporary)) return Status::IoError;
        std::error_code error;std::filesystem::rename(temporary,root/"cut.bin",error);
        if(error || !syncCorpusFile(root,true)) return Status::IoError;
        return Status::Ok;
    } catch(...) {return Status::IoError;}
}
Status readCorpusCut(const std::filesystem::path& root,std::shared_ptr<const CorpusCut>& cut,std::string& error,std::uint64_t maximumMetadataBytes) noexcept {
    error.clear();
    try {
        std::ifstream file(root/"cut.bin",std::ios::binary|std::ios::ate);
        if(!file) {error="durable corpus cut is unavailable";return Status::IoError;}
        if(!ownedCorpusFile(root/"cut.bin")) {error="corpus cut file ownership mismatch";return Status::CorruptData;}
        const auto end=file.tellg();
        if(end<static_cast<std::streamoff>(sizeof(CutHeader))) {error="corpus cut is truncated";return Status::CorruptData;}
        CutHeader header{};file.seekg(0);file.read(reinterpret_cast<char*>(&header),sizeof(header));
        const auto maximum=header.manifest.maximumBytes;
        if(!validCutHeader(header,static_cast<std::uint64_t>(end)) || !file || header.magic!=CutHeader{}.magic ||
           header.bytes!=static_cast<std::uint64_t>(end) ||
           !header.generation || !maximum || header.bytes>maximum || header.sources==0 ||
           header.sources>kBinaryMarketMaximumSources || header.sources!=header.manifest.sourceCount ||
           header.gaps>header.sources*kBinaryMarketStoredChannelCount || header.blocks>maximum/sizeof(CorpusBlockRange) ||
           header.referenceBytes>kCorpusMaximumReferenceBytes || header.finalized>1 || header.reserved ||
           header.manifest.magic!=kBinaryMarketManifestMagic ||
           header.progress.producerEpoch!=header.manifest.producerEpoch || !header.progress.shardCount ||
           header.progress.shardCount!=header.manifest.shardCount || header.progress.shardCount>kBinaryMarketMaximumShards) {
            error="corpus cut header mismatch";return Status::CorruptData;
        }
        if(header.bytes>maximumMetadataBytes) {error="corpus metadata exceeds resident input budget";return Status::OutOfRange;}
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(header.bytes));
        file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        const auto stored=header.crc;header.crc=0;cxet::bytescan::hftMemcpy(bytes.data(),&header,sizeof(header));
        if(!file || crc(bytes)!=stored) {error="corpus cut CRC mismatch";return Status::CorruptData;}
        auto value=std::make_shared<CorpusCut>();value->generation=header.generation;
        value->committedMonotonicNs=header.committedMonotonicNs;value->progress=header.progress;
        value->manifest=header.manifest;value->finalized=header.finalized!=0;
        std::span<const std::uint8_t> payload(bytes.data()+sizeof(header),bytes.size()-sizeof(header));
        if(!unpack(payload,header.sources,value->sources) || !unpack(payload,header.gaps,value->gaps) ||
           !unpack(payload,header.blocks,value->blocks) || !unpack(payload,header.referenceBytes,value->referencePayload) || !payload.empty()) {
            error="corpus cut table bounds mismatch";return Status::CorruptData;
        }
        if(crc({reinterpret_cast<const std::uint8_t*>(value->sources.data()),value->sources.size()*sizeof(BinaryMarketSource)})!=header.sourcesCrc) {
            error="corpus cut source CRC mismatch";return Status::CorruptData;
        }
        for(std::size_t n=0;n<value->sources.size();++n) if(!validBinaryMarketSource(value->sources[n],n)) {
            error="corpus cut source mismatch";return Status::CorruptData;
        }
        std::vector<bool> gapSeen(value->sources.size()*kBinaryMarketStoredChannelCount,false);
        for(const auto& gap:value->gaps) {
            if(!gap.sourceId || gap.sourceId>value->sources.size() || !gap.sourceGeneration || !gap.gapEpoch ||
               gap.gapEpoch!=gap.droppedRecords || !gap.firstDroppedEventSequence || !gap.lastDroppedEventSequence ||
               gap.observedReceiveNs<=0 || gap.shardIndex>=value->progress.shardCount ||
               !validBinaryMarketRecordChannel(gap.channel) || gap.reserved32 || gap.reserved8 ||
               !((gap.minimumDroppedReceiveNs>0 && gap.maximumDroppedReceiveNs>=gap.minimumDroppedReceiveNs) ||
                 (!gap.minimumDroppedReceiveNs && !gap.maximumDroppedReceiveNs))) {
                error="corpus cut gap mismatch";return Status::CorruptData;
            }
            const auto index=(gap.sourceId-1u)*kBinaryMarketStoredChannelCount+binaryMarketChannelIndex(gap.channel);
            if(gapSeen[index]) {error="duplicate corpus cut gap cell";return Status::CorruptData;}
            gapSeen[index]=true;
        }
        std::uint64_t records{};
        for(const auto& range:value->blocks) {
            const auto& block=range.header;auto check=block;check.headerCrc32c=0;
            if(!range.segmentNumber || range.shardIndex>=value->progress.shardCount || range.reserved ||
               range.offset<sizeof(BinaryMarketSegmentHeader) || range.offset>maximum ||
               !range.firstMonotonicNs || range.lastMonotonicNs<range.firstMonotonicNs ||
               block.magic!=kBinaryMarketBlockMagic ||
               block.headerBytes!=sizeof(block) || !block.blockNumber || !block.recordCount || block.recordCount>kBinaryMarketBlockRecords ||
               !validBinaryMarketRecordChannel(block.channel) || block.rawBytes!=block.recordCount*binaryMarketBlockLayout(block.channel).recordBytes ||
               !block.encodedBytes || block.encodedBytes>hft_compressor::byteBlockEncodeBound(binaryMarketBlockLayout(block.channel),block.recordCount) ||
               block.reserved!=decltype(block.reserved){} || !block.firstShardSequence || block.lastShardSequence<block.firstShardSequence ||
               codec::crc32c(reinterpret_cast<const std::uint8_t*>(&check),sizeof(check))!=block.headerCrc32c ||
               records>UINT64_MAX-block.recordCount) {
                error="corpus cut block reference mismatch";return Status::CorruptData;
            }
            records+=block.recordCount;
        }
        if(records!=value->manifest.recordCount || value->gaps.size()!=value->manifest.gapCount) {
            error="corpus cut committed counts mismatch";return Status::CorruptData;
        }
        cut=std::move(value);return Status::Ok;
    } catch(...) {error="corpus cut read failed";return Status::IoError;}
}
Status readCorpusCompressedBlock(const std::filesystem::path& root,const CorpusCut& cut,
    std::size_t index,std::vector<std::uint8_t>& bytes,std::string& error,
    std::shared_ptr<CorpusPublication> publication) noexcept {
    error.clear();bytes.clear();
    if(index>=cut.blocks.size()) return Status::InvalidArgument;
    try {
        const auto& range=cut.blocks[index];const auto& block=range.header;
        auto check=block;check.headerCrc32c=0;
        if(!validBinaryMarketRecordChannel(block.channel) || !block.recordCount || block.recordCount>kBinaryMarketBlockRecords ||
           block.rawBytes!=block.recordCount*binaryMarketBlockLayout(block.channel).recordBytes ||
           block.encodedBytes>hft_compressor::byteBlockEncodeBound(binaryMarketBlockLayout(block.channel),block.recordCount) ||
           !block.encodedBytes || codec::crc32c(reinterpret_cast<const std::uint8_t*>(&check),sizeof(check))!=block.headerCrc32c) {
            error="compressed corpus range contract mismatch";return Status::CorruptData;
        }
        bytes.resize(sizeof(block)+block.encodedBytes);cxet::bytescan::hftMemcpy(bytes.data(),&block,sizeof(block));
        auto payload=std::span<std::uint8_t>(bytes).subspan(sizeof(block));
        if(!publication || !publication->load(range,payload)) {
            std::ifstream file(blockPath(root,range,"cxm"),std::ios::binary);
            if(!file) {file.clear();file.open(blockPath(root,range,"partial"),std::ios::binary);}
            if(!file) {file.clear();file.open(blockPath(root,range,"cxm"),std::ios::binary);}
            file.seekg(static_cast<std::streamoff>(range.offset));
            file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            if(!file || cxet::bytescan::hftMemcmp(bytes.data(),&block,sizeof(block))) {
                error="compressed corpus archive range unavailable";bytes.clear();return Status::IoError;
            }
        }
        if(crc(payload)!=block.encodedCrc32c) {error="compressed corpus range CRC mismatch";bytes.clear();return Status::CorruptData;}
        return Status::Ok;
    } catch(...) {bytes.clear();error="compressed corpus range read failed";return Status::IoError;}
}
Status readCorpusRetainedCompressedBlock(std::shared_ptr<CorpusPublication> publication,
    const CorpusCut& cut,std::size_t index,std::vector<std::uint8_t>& bytes,
    bool& available,std::string& error) noexcept {
    bytes.clear();available=false;error.clear();
    if(!publication || index>=cut.blocks.size()) return Status::InvalidArgument;
    try {
        const auto& range=cut.blocks[index];const auto& block=range.header;
        if(!validBinaryMarketRecordChannel(block.channel) || !block.recordCount || block.recordCount>kBinaryMarketBlockRecords ||
           block.rawBytes!=block.recordCount*binaryMarketBlockLayout(block.channel).recordBytes ||
           !block.encodedBytes || block.encodedBytes>hft_compressor::byteBlockEncodeBound(binaryMarketBlockLayout(block.channel),block.recordCount))
            return Status::CorruptData;
        bytes.resize(sizeof(block)+block.encodedBytes);cxet::bytescan::hftMemcpy(bytes.data(),&block,sizeof(block));
        auto payload=std::span<std::uint8_t>(bytes).subspan(sizeof(block));
        if(!publication->load(range,payload)) {bytes.clear();return Status::Ok;}
        if(crc(payload)!=block.encodedCrc32c) {bytes.clear();error="retained compressed block CRC mismatch";return Status::CorruptData;}
        available=true;return Status::Ok;
    } catch(...) {bytes.clear();error="retained compressed block read failed";return Status::IoError;}
}
struct CorpusActiveCursorState final {
    struct Shard final {std::size_t block{};std::uint32_t position{},count{};
        std::vector<BinaryMarketRecord> decoded{};std::uint64_t previousSequence{},previousTime{};bool ready{};
        BinaryMarketDepthTransactionState depthTransaction{};};
    std::filesystem::path root{};std::shared_ptr<CorpusPublication> publication{};
    std::shared_ptr<const CorpusCut> cut{};std::vector<Shard> shards{};
    std::vector<std::uint8_t> encoded{};hft_compressor::ByteBlockWorkspace workspace{};
    std::uint64_t begin{};bool archive{},failed{},feedDetached{},caughtUp{};
    CorpusCompressedBlockSource source{};std::vector<std::uint8_t> remoteBytes{};std::uint64_t maximumResidentBytes{};
};
CorpusActiveCursor::CorpusActiveCursor() noexcept=default;
CorpusActiveCursor::~CorpusActiveCursor() noexcept=default;
CorpusActiveCursor::CorpusActiveCursor(CorpusActiveCursor&&) noexcept=default;
CorpusActiveCursor& CorpusActiveCursor::operator=(CorpusActiveCursor&&) noexcept=default;
Status CorpusActiveCursor::open(const std::filesystem::path& root,std::uint64_t begin,std::string& error,
    std::shared_ptr<CorpusPublication> publication,CorpusCompressedBlockSource source,std::uint64_t maximumResidentBytes) noexcept {
    try {
        auto state=std::make_unique<CorpusActiveCursorState>();state->source=source;state->root=root;state->begin=begin;
        state->maximumResidentBytes=maximumResidentBytes;
        state->publication=std::move(publication);
        state_=std::move(state);
        return refresh(error);
    } catch(...) {error="active cursor allocation failed";return Status::IoError;}
}
Status CorpusActiveCursor::refresh(std::string& error) noexcept {
    if(!state_ || state_->failed) return Status::InvalidArgument;
    try {
        auto next=state_->publication?state_->publication->latest.load(std::memory_order_acquire):nullptr;
        const bool locallyAcknowledged=static_cast<bool>(next);
        std::uint64_t metadataBytes{},shardCount{};
        if(next) {
            metadataBytes=sizeof(CutHeader)+next->sources.size()*sizeof(BinaryMarketSource)+next->gaps.size()*sizeof(BinaryMarketGap)+
                next->blocks.size()*sizeof(CorpusBlockRange)+next->referencePayload.size();
            shardCount=next->progress.shardCount;
        } else {
            CorpusSummary summary{};const auto result=readCorpusSummary(state_->root,summary,error);
            if(!isOk(result))return result;
            metadataBytes=summary.cutBytes;shardCount=summary.manifest.shardCount;
        }
        const auto encodedBound=hft_compressor::byteBlockEncodeBound(
            binaryMarketBlockLayout(BinaryMarketChannel::Depth),kBinaryMarketBlockRecords);
        // Includes previous/candidate cut, serialized read staging, per-shard
        // decode buffers, both compressed buffers and prepared codec workspace.
        const std::uint64_t fixed=8u*1024u*1024u+shardCount*kBinaryMarketBlockRecords*sizeof(BinaryMarketRecord)+2u*encodedBound;
        if(fixed>state_->maximumResidentBytes || metadataBytes>(state_->maximumResidentBytes-fixed)/3u) {
            error="active cursor exceeds resident input budget";return Status::OutOfRange;
        }
        if(!next) {const auto result=readCorpusCut(state_->root,next,error,(state_->maximumResidentBytes-fixed)/3u);if(!isOk(result))return result;}
        metadataBytes=sizeof(CutHeader)+next->sources.size()*sizeof(BinaryMarketSource)+next->gaps.size()*sizeof(BinaryMarketGap)+
            next->blocks.size()*sizeof(CorpusBlockRange)+next->referencePayload.size();
        const std::uint64_t actualFixed=8u*1024u*1024u+
            next->progress.shardCount*kBinaryMarketBlockRecords*sizeof(BinaryMarketRecord)+2u*encodedBound;
        if(actualFixed>state_->maximumResidentBytes || metadataBytes>(state_->maximumResidentBytes-actualFixed)/3u) {
            error="active cursor updated cut exceeds resident input budget";return Status::OutOfRange;
        }
        const bool archivedAuthority=finalizedCutIsDurable(state_->root,*next);
        if(!locallyAcknowledged && !archivedAuthority) {
            // Atomic rename visibility precedes directory fsync. A running
            // Recorder's post-fsync acknowledgement authorizes active rows.
            if(!state_->source.approvedGeneration) {
                error="active corpus has no durable generation authority";return Status::InvalidArgument;
            }
            std::uint64_t approved{};
            const auto result=state_->source.approvedGeneration(state_->source.context,approved,error);
            if(!isOk(result))return result;
            if(next->generation>approved) {
                if(state_->cut)return Status::Ok;
                error="awaiting durable Recorder cut acknowledgement";return Status::Unknown;
            }
        }
        if(archivedAuthority) {state_->feedDetached=true;state_->archive=true;}
        if(state_->cut && (next->generation<state_->cut->generation ||
           next->progress.producerEpoch!=state_->cut->progress.producerEpoch ||
           next->blocks.size()<state_->cut->blocks.size() || next->progress.safeMonotonicNs<state_->cut->progress.safeMonotonicNs ||
           next->progress.shardCount!=state_->cut->progress.shardCount)) {
            error="corpus cut continuity failed";state_->failed=true;return Status::CorruptData;
        }
        if(state_->cut && state_->cut->manifest.recordCount &&
           state_->cut->progress.safeMonotonicNs>state_->cut->manifest.replayAnchorMonotonicNs &&
           (next->manifest.replayAnchorMonotonicNs!=state_->cut->manifest.replayAnchorMonotonicNs ||
            next->manifest.replayAnchorReceiveNs!=state_->cut->manifest.replayAnchorReceiveNs)) {
            error="executable corpus replay anchor changed";state_->failed=true;return Status::CorruptData;
        }
        if(state_->cut) for(std::size_t n=0;n<state_->cut->blocks.size();++n) {
            const auto& old=state_->cut->blocks[n];const auto& current=next->blocks[n];
            if(old.offset!=current.offset || old.segmentNumber!=current.segmentNumber || old.shardIndex!=current.shardIndex ||
               cxet::bytescan::hftMemcmp(&old.header,&current.header,sizeof(old.header))) {
                error="corpus immutable block prefix changed";state_->failed=true;return Status::CorruptData;
            }
        }
        if(state_->shards.empty()) {
            if(state_->workspace.prepare()!=hft_compressor::Status::Ok) {
                error="active cursor workspace unavailable";return Status::IoError;
            }
            state_->encoded.resize(encodedBound);
            state_->shards.resize(next->progress.shardCount);
            for(auto& shard:state_->shards) shard.decoded.resize(kBinaryMarketBlockRecords);
        }
        if(!archivedAuthority && state_->caughtUp && state_->cut && next->generation>state_->cut->generation) {
            state_->feedDetached=false;state_->caughtUp=false;
        }
        state_->cut=std::move(next);return Status::Ok;
    } catch(...) {error="active cursor refresh failed";return Status::IoError;}
}
Status CorpusActiveCursor::next(BinaryMarketRecord& output,CorpusCursorResult& result,std::string& error) noexcept {
    result=CorpusCursorResult::WouldBlock;error.clear();
    if(!state_ || state_->failed || !state_->cut) return Status::InvalidArgument;
    try {
        auto& state=*state_;const auto& cut=*state.cut;
        for(std::size_t s=0;s<state.shards.size();++s) {
            auto& shard=state.shards[s];
            while(!shard.ready) {
                if(shard.position<shard.count) {shard.ready=true;break;}
                while(shard.block<cut.blocks.size() && cut.blocks[shard.block].shardIndex!=s) ++shard.block;
                if(shard.block==cut.blocks.size())break;
                const auto& range=cut.blocks[shard.block];const auto& block=range.header;
                auto check=block;check.headerCrc32c=0;
                const auto layout=binaryMarketBlockLayout(block.channel);
                const auto bound=hft_compressor::byteBlockEncodeBound(layout,block.recordCount);
                if(!range.segmentNumber || range.offset<sizeof(BinaryMarketSegmentHeader) ||
                    block.magic!=kBinaryMarketBlockMagic ||
                    block.headerBytes!=sizeof(block) || block.reserved!=decltype(block.reserved){} ||
                    !validBinaryMarketRecordChannel(block.channel) || !block.recordCount || block.recordCount>kBinaryMarketBlockRecords ||
                    block.rawBytes!=block.recordCount*layout.recordBytes || !block.encodedBytes ||
                    block.encodedBytes>bound || block.encodedBytes>state.encoded.size() ||
                    codec::crc32c(reinterpret_cast<const std::uint8_t*>(&check),sizeof(check))!=block.headerCrc32c) {
                    error="active corpus block range mismatch";state.failed=true;return Status::CorruptData;
                }
                auto encoded=std::span<std::uint8_t>(state.encoded.data(),block.encodedBytes);
                bool inMemory=state.publication && state.publication->load(range,encoded);
                if(!inMemory && state.source.fetch && !state.feedDetached) {
                    bool available=false;
                    const auto status=state.source.fetch(state.source.context,cut.generation,shard.block,state.remoteBytes,available,error);
                    if(!isOk(status)) {state.failed=true;return status;}
                    if(available) {
                        if(state.remoteBytes.size()!=sizeof(block)+block.encodedBytes ||
                           cxet::bytescan::hftMemcmp(state.remoteBytes.data(),&block,sizeof(block))) {
                            error="compressed feed block identity mismatch";state.failed=true;return Status::CorruptData;
                        }
                        std::copy(state.remoteBytes.begin()+sizeof(block),state.remoteBytes.end(),encoded.begin());inMemory=true;
                    } else state.feedDetached=true;
                }
                if(!inMemory) {
                    state.archive=true;
                    std::ifstream file(blockPath(state.root,range,"cxm"),std::ios::binary);
                    if(!file) {file.clear();file.open(blockPath(state.root,range,"partial"),std::ios::binary);}
                    // Rename may race the first open; retry the sealed name once.
                    if(!file) {file.clear();file.open(blockPath(state.root,range,"cxm"),std::ios::binary);}
                    BinaryMarketBlockHeader disk{};file.seekg(static_cast<std::streamoff>(range.offset));
                    file.read(reinterpret_cast<char*>(&disk),sizeof(disk));
                    file.read(reinterpret_cast<char*>(encoded.data()),block.encodedBytes);
                    if(!file || cxet::bytescan::hftMemcmp(&disk,&block,sizeof(block))!=0) {
                        error="active corpus archive block unavailable";state.failed=true;return Status::IoError;
                    }
                }
                std::size_t raw{};
                auto* rawData=reinterpret_cast<std::uint8_t*>(shard.decoded.data());
                if(crc(encoded)!=block.encodedCrc32c ||
                   hft_compressor::decodeByteBlock(state.workspace,layout,encoded,
                    std::span<std::uint8_t>(rawData,block.rawBytes),raw)!=hft_compressor::Status::Ok ||
                   raw!=block.rawBytes || codec::crc32c(rawData,raw)!=block.rawCrc32c) {
                    error="active corpus block decode mismatch";state.failed=true;return Status::CorruptData;
                }
                std::uint64_t previousSequence=shard.previousSequence,previousTime=shard.previousTime;
                for(std::uint32_t n=0;n<block.recordCount;++n) {
                    const auto& record=shard.decoded[n];const auto& h=record.header;
                    if(!validBinaryMarketRecord(record) || h.shardIndex!=s || h.sourceId>cut.sources.size() || h.channel!=block.channel ||
                       (previousSequence && h.shardSequence<=previousSequence) || (previousTime && h.receiveMonotonicNs<previousTime) ||
                       !std::all_of(record.payload.begin()+h.payloadBytes,record.payload.end(),[](std::byte byte) {return byte==std::byte{};})) {
                        error="active corpus record mismatch";state.failed=true;return Status::CorruptData;
                    }
                    previousSequence=h.shardSequence;previousTime=h.receiveMonotonicNs;
                }
                if(shard.decoded.front().header.shardSequence!=block.firstShardSequence ||
                   shard.decoded[block.recordCount-1].header.shardSequence!=block.lastShardSequence ||
                   shard.decoded.front().header.receiveMonotonicNs!=range.firstMonotonicNs ||
                   shard.decoded[block.recordCount-1].header.receiveMonotonicNs!=range.lastMonotonicNs) {
                    error="active corpus block sequence mismatch";state.failed=true;return Status::CorruptData;
                }
                ++shard.block;shard.previousSequence=previousSequence;shard.previousTime=previousTime;
                shard.count=block.recordCount;shard.position=0;
                while(shard.position<shard.count && shard.decoded[shard.position].header.receiveMonotonicNs<state.begin) ++shard.position;
            }
        }
        std::size_t best=state.shards.size();
        for(std::size_t n=0;n<state.shards.size();++n) if(state.shards[n].ready &&
            (best==state.shards.size() || before(state.shards[n].decoded[state.shards[n].position],state.shards[best].decoded[state.shards[best].position]))) best=n;
        if(best==state.shards.size()) {
            if(cut.finalized) for(const auto& shard:state.shards) if(shard.depthTransaction.active()) {
                error="finalized corpus ends inside depth transaction";state.failed=true;return Status::CorruptData;
            }
            state.caughtUp=true;result=cut.finalized?CorpusCursorResult::End:CorpusCursorResult::WouldBlock;return Status::Ok;
        }
        auto& shard=state.shards[best];const auto& record=shard.decoded[shard.position];
        if(!cut.progress.safeMonotonicNs || record.header.receiveMonotonicNs>=cut.progress.safeMonotonicNs ||
           record.header.shardSequence>cut.progress.shardSequences[best]) {state.caughtUp=true;return Status::Ok;}
        if(!advanceBinaryMarketDepthTransaction(record,shard.depthTransaction)) {
            error="active corpus depth transaction mismatch";state.failed=true;return Status::CorruptData;
        }
        copyBinaryMarketRecord(output,record);++shard.position;shard.ready=shard.position<shard.count;
        result=CorpusCursorResult::Record;return Status::Ok;
    } catch(...) {error="active corpus cursor failed";return Status::IoError;}
}
std::shared_ptr<const CorpusCut> CorpusActiveCursor::cut() const noexcept {return state_?state_->cut:nullptr;}
bool CorpusActiveCursor::usingArchive() const noexcept {return state_ && state_->archive;}
} // namespace hftrec::corpus
