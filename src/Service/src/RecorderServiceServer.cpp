#include "hftrec/Service/RecorderService.hpp"
#include "RecorderServiceProtocol.hpp"
#include "../../Runtime/src/Capture/Coordinator/RecorderCaptureSession.hpp"
#include <atomic>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <thread>

namespace hftrec::service {
struct RecorderServiceServerState final {
    int listener{-1};
    std::filesystem::path runtime{},socketPath{};
    std::vector<int> peers{};
    RecorderServiceConfig config{};
    capture::RecorderCaptureSession capture{};
    bool configured{}, stopping{};
    std::atomic<bool> stopComplete{true};
    std::thread stopWorker{};
    dev_t socketDevice{};ino_t socketInode{};
};
namespace {
capture::RecorderCaptureSessionConfig captureConfig(const RecorderServiceConfig& value) {
    capture::RecorderCaptureSessionConfig result;
    result.workspaceRoot=value.workspaceRoot;result.parserTemplate=value.parserTemplate;result.envPath=value.envPath;
    result.outputRoot=value.outputRoot;result.channelMask=value.channelMask;result.durationSec=value.durationSec;
    result.maximumBytes=value.maximumBytes;result.segmentBytes=value.segmentBytes;
    result.retainedCompressedBytes=value.retainedCompressedBytes;result.maximumReferenceBytes=value.maximumReferenceBytes;
    result.writerQueueRecords=value.writerQueueRecords;
    for(const auto& venue:value.venues)result.venues.push_back({venue.exchange,venue.market,venue.instruments,venue.fullUniverse});
    return result;
}
void reapStop(RecorderServiceServerState& state) {
    if(state.stopping && state.stopComplete.load(std::memory_order_acquire)) {
        if(state.stopWorker.joinable())state.stopWorker.join();state.stopping=false;
    }
}
Status configureState(RecorderServiceServerState& state,const RecorderServiceConfig& config,std::string& error) {
    reapStop(state);
    if(state.stopping) {error="Recorder is stopping";return Status::Cancelled;}
    if(config.channelMask==0u || config.channelMask>255u || !config.durationSec || config.durationSec>std::numeric_limits<std::uint64_t>::max()/1'000'000'000u ||
       config.writerQueueRecords==0u || config.writerQueueRecords>65536u || config.maximumBytes==0u || config.segmentBytes<4096u ||
       config.segmentBytes>config.maximumBytes || config.retainedCompressedBytes>1024u*1024u*1024u ||
       config.maximumReferenceBytes<1024u*1024u || config.maximumReferenceBytes>128u*1024u*1024u ||
       !config.workspaceRoot.is_absolute() || !config.parserTemplate.is_absolute() || !config.envPath.is_absolute() ||
       !config.outputRoot.is_absolute() || config.outputRoot==config.outputRoot.root_path() ||
       config.outputRoot.lexically_normal()!=std::filesystem::weakly_canonical(config.outputRoot)) {
        error="Recorder configuration has an invalid path or explicit storage/RAM bound";return Status::InvalidArgument;
    }
    detail::Encoder bounded;detail::encodeConfig(bounded,config);
    if(!bounded.valid) {error="Recorder selection exceeds the control message bound";return Status::OutOfRange;}
    auto selected=captureConfig(config);
    if(state.capture.snapshot().active) {
        const auto status=state.capture.updateSelection(selected,error);if(!isOk(status))return status;
    } else {
        if(!std::filesystem::is_regular_file(config.workspaceRoot/"tools/Build/ActiveBuild.py") ||
           !std::filesystem::is_regular_file(config.parserTemplate) || std::filesystem::file_size(config.parserTemplate)>1024u*1024u ||
           std::filesystem::weakly_canonical(config.envPath)!=std::filesystem::weakly_canonical(config.parserTemplate.parent_path()/".env")) {
            error="Recorder needs an existing Parser template and its producer-owned .env path";return Status::InvalidArgument;
        }
        std::ifstream file(config.parserTemplate,std::ios::binary);
        if(!file) {error="Recorder cannot read the selected Parser template";return Status::IoError;}
        std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()},rendered;
        const auto status=capture::renderRecorderParserConfig(text,selected,rendered,error);if(!isOk(status))return status;
    }
    state.config=config;state.configured=true;return Status::Ok;
}
Status startState(RecorderServiceServerState& state,std::string& error) {
    reapStop(state);
    if(!state.configured || state.stopping) {error="Recorder requires a configured, stopped session";return Status::InvalidArgument;}
    return state.capture.start(captureConfig(state.config),error);
}
void stopState(RecorderServiceServerState& state) {
    reapStop(state);if(state.stopping)return;
    state.stopComplete.store(false,std::memory_order_release);
    try {state.stopWorker=std::thread([&state] {(void)state.capture.stop();state.stopComplete.store(true,std::memory_order_release);});}
    catch(...) {state.stopComplete.store(true,std::memory_order_release);throw;}
    state.stopping=true;
}
RecorderServiceStatus statusState(RecorderServiceServerState& state) {
    reapStop(state);const auto captured=state.capture.snapshot();const auto cut=state.capture.latestCut();RecorderServiceStatus result;
    result.configured=state.configured;result.active=captured.active;result.connected=captured.connected;result.complete=captured.complete;
    result.stopping=state.stopping;result.selectionPending=captured.selectionPending;result.captureStatus=captured.status;
    result.producerEpoch=captured.producerEpoch;result.sourceCount=captured.sourceCount;result.sessionPath=captured.sessionPath;
    result.records=captured.writer.recordCount;result.gaps=captured.writer.gapCount;result.projectedBytes=captured.writer.projectedBytes;
    result.error=captured.error;
    if(cut) {result.cutGeneration=cut->generation;result.safeMonotonicNs=cut->progress.safeMonotonicNs;result.lossEpoch=cut->progress.lossEpoch;
        result.startedReceiveNs=cut->manifest.startedReceiveNs;result.startedMonotonicNs=cut->manifest.replayAnchorMonotonicNs;
        result.sourceIdentity=corpus::corpusIdentity(cut->manifest);}
    return result;
}
bool knownSessionPath(const RecorderServiceServerState& state,const std::filesystem::path& path) {
    if(!state.configured || !path.is_absolute() || path.lexically_normal()!=path ||
       path.parent_path()!=state.config.outputRoot)return false;
    std::error_code error;const auto status=std::filesystem::symlink_status(path,error);
    return !error && std::filesystem::is_directory(status) && std::filesystem::canonical(path,error)==path && !error;
}
Status sessionEntries(RecorderServiceServerState& state,std::vector<RecorderSessionEntry>& output,std::string& error) {
    output.clear();if(!state.configured)return Status::Ok;
    std::error_code ec;const auto root=state.config.outputRoot;
    if(!std::filesystem::exists(root,ec))return ec?Status::IoError:Status::Ok;
    std::filesystem::directory_iterator current(root,ec),end;
    if(ec) {error="Recorder archive root is unreadable";return Status::IoError;}
    std::uint32_t inspected=0u;const auto live=state.capture.snapshot();const auto liveCut=state.capture.latestCut();
    while(current!=end) {
        if(++inspected>4096u) {error="Recorder archive catalog exceeds its 4096-entry bound";return Status::OutOfRange;}
        const auto path=current->path();const auto type=current->symlink_status(ec);
        if(ec) {error="Recorder archive entry is unreadable";return Status::IoError;}
        if(std::filesystem::is_directory(type)) {
            corpus::CorpusSummary summary;std::string unavailable;
            bool have=false;
            if(liveCut && path==live.sessionPath) {summary.manifest=liveCut->manifest;summary.cutGeneration=liveCut->generation;
                summary.finalized=liveCut->finalized;have=true;}
            else have=isOk(corpus::readCorpusSummary(path,summary,unavailable));
            if(have) {
                RecorderSessionEntry entry;entry.sourcePath=path;entry.sourceIdentity=corpus::corpusIdentity(summary.manifest);
                entry.producerEpoch=summary.manifest.producerEpoch;entry.cutGeneration=summary.cutGeneration;
                entry.startedReceiveNs=summary.manifest.startedReceiveNs;
                entry.endedReceiveNs=summary.finalized?summary.manifest.finalizedReceiveNs:summary.manifest.lastReceiveNs;
                entry.sourceCount=summary.manifest.sourceCount;entry.active=live.active && path==live.sessionPath;
                entry.complete=summary.finalized && summary.manifest.complete!=0u;output.push_back(std::move(entry));
            }
        }
        current.increment(ec);if(ec) {error="Recorder archive discovery failed";return Status::IoError;}
    }
    std::sort(output.begin(),output.end(),[](const auto& a,const auto& b){return a.sourcePath<b.sourcePath;});return Status::Ok;
}
Status handle(RecorderServiceServerState& state,detail::Operation operation,std::span<const std::uint8_t> payload,
    detail::Encoder& output,std::string& error) {
    detail::Decoder input(payload);
    switch(operation) {
        case detail::Operation::Configure: {
            RecorderServiceConfig config;detail::decodeConfig(input,config);
            if(!input.done()) {error="Recorder configuration packet rejected";return Status::InvalidArgument;}
            return configureState(state,config,error);
        }
        case detail::Operation::Start:
            if(!input.done())return Status::InvalidArgument;
            return startState(state,error);
        case detail::Operation::Stop:
            if(!input.done())return Status::InvalidArgument;
            stopState(state);return Status::Ok;
        case detail::Operation::Status:
            if(!input.done())return Status::InvalidArgument;
            detail::encodeStatus(output,statusState(state));return Status::Ok;
        case detail::Operation::Catalog: {
            std::uint32_t offset{},limit{};input.pod(offset);input.pod(limit);
            if(!input.done() || limit==0u || limit>256u)return Status::InvalidArgument;
            const auto cut=state.capture.latestCut();const auto total=cut?static_cast<std::uint32_t>(cut->sources.size()):0u;
            if(offset>total)return Status::OutOfRange;
            const auto count=std::min(limit,total-offset);output.pod(cut?cut->generation:0u);output.pod(total);output.pod(offset+count);output.pod(count);
            for(std::uint32_t i=0u;i<count;++i)output.pod(cut->sources[offset+i]);return Status::Ok;
        }
        case detail::Operation::Block: {
            std::uint64_t generation{};std::uint32_t index{},offset{},maximum{};
            input.pod(generation);input.pod(index);input.pod(offset);input.pod(maximum);
            if(!input.done() || maximum==0u || maximum>kRecorderServiceMaximumBlockChunk)return Status::InvalidArgument;
            const auto cut=state.capture.latestCut();
            if(!cut || index>=cut->blocks.size() || (generation && generation!=cut->generation)) {
                error="Recorder cut generation or block is unavailable; refresh the durable cut";return Status::OutOfRange;
            }
            std::vector<std::uint8_t> bytes;bool available=false;
            const auto status=corpus::readCorpusRetainedCompressedBlock(state.capture.publication(),*cut,index,bytes,available,error);
            if(!isOk(status))return status;
            const auto& range=cut->blocks[index];
            const auto total=static_cast<std::uint64_t>(sizeof(range.header))+range.header.encodedBytes;
            if(offset>total || (available && bytes.size()!=total))return Status::CorruptData;
            const auto count=available?static_cast<std::uint32_t>(std::min<std::uint64_t>(maximum,total-offset)):0u;
            output.pod(cut->generation);output.pod(total);output.pod(offset);output.boolean(false);output.boolean(available);output.boolean(!available);
            output.pod(range);output.pod(count);
            if(count)output.bytes.insert(output.bytes.end(),bytes.begin()+offset,bytes.begin()+offset+count);
            return Status::Ok;
        }
        case detail::Operation::Sessions: {
            std::uint32_t offset{},limit{};input.pod(offset);input.pod(limit);
            if(!input.done() || limit==0u || limit>64u)return Status::InvalidArgument;
            std::vector<RecorderSessionEntry> entries;const auto result=sessionEntries(state,entries,error);if(!isOk(result))return result;
            const auto total=static_cast<std::uint32_t>(entries.size());if(offset>total)return Status::OutOfRange;
            const auto count=std::min(limit,total-offset);output.pod(total);output.pod(offset+count);output.pod(count);
            for(std::uint32_t i=0u;i<count;++i) {
                const auto& value=entries[offset+i];output.path(value.sourcePath);output.string(value.sourceIdentity);
                output.pod(value.producerEpoch);output.pod(value.cutGeneration);output.pod(value.startedReceiveNs);
                output.pod(value.endedReceiveNs);output.pod(value.sourceCount);output.boolean(value.active);output.boolean(value.complete);
            }
            return Status::Ok;
        }
        case detail::Operation::SessionSources: {
            std::filesystem::path path;std::uint32_t offset{},limit{};input.path(path);input.pod(offset);input.pod(limit);
            if(!input.done() || limit==0u || limit>256u || !knownSessionPath(state,path))return Status::InvalidArgument;
            const auto live=state.capture.snapshot();const auto cut=state.capture.latestCut();
            std::vector<corpus::BinaryMarketSource> archived;std::span<const corpus::BinaryMarketSource> sources;
            std::uint64_t generation{};
            if(cut && path==live.sessionPath) {sources=cut->sources;generation=cut->generation;}
            else {
                corpus::CorpusSummary summary;auto result=corpus::readCorpusSummary(path,summary,error);if(!isOk(result))return result;
                result=corpus::readCorpusSourceCatalog(path,archived,error);if(!isOk(result))return result;
                corpus::CorpusSummary confirmed;result=corpus::readCorpusSummary(path,confirmed,error);if(!isOk(result))return result;
                if(confirmed.cutGeneration!=summary.cutGeneration || corpus::corpusIdentity(confirmed.manifest)!=corpus::corpusIdentity(summary.manifest)) {
                    error="Recorder source catalog changed during acquisition; refresh its committed metadata";return Status::Cancelled;
                }
                generation=summary.cutGeneration;sources=archived;
            }
            const auto total=static_cast<std::uint32_t>(sources.size());if(offset>total)return Status::OutOfRange;
            const auto count=std::min(limit,total-offset);output.pod(generation);output.pod(total);output.pod(offset+count);output.pod(count);
            for(std::uint32_t i=0u;i<count;++i)output.pod(sources[offset+i]);return Status::Ok;
        }
    }
    return Status::InvalidArgument;
}
}
RecorderServiceServer::RecorderServiceServer() noexcept=default;
RecorderServiceServer::~RecorderServiceServer() noexcept {close();}
Status RecorderServiceServer::open(const std::filesystem::path& runtime,std::string& error) noexcept {
    close();error.clear();
    try {
        if(::mkdir(runtime.c_str(),0700)!=0 && errno!=EEXIST) {error="cannot create private Recorder runtime directory";return Status::IoError;}
        if(!detail::privateDirectory(runtime,error))return Status::InvalidArgument;
        sockaddr_un address{};if(!detail::addressFor(runtime,address,error))return Status::InvalidArgument;
        auto state=std::make_unique<RecorderServiceServerState>();state->runtime=runtime;state->socketPath=runtime/kRecorderServiceSocket;
        struct stat existing{};
        if(::lstat(state->socketPath.c_str(),&existing)==0 || errno!=ENOENT) {error="Recorder socket already exists; refusing to replace another owner or stale endpoint";return Status::InvalidArgument;}
        state->listener=::socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
        if(state->listener<0) {error="cannot create Recorder listener";return Status::IoError;}
        if(::bind(state->listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0) {
            ::close(state->listener);error="Recorder endpoint bind failed";return Status::IoError;
        }
        if(::chmod(state->socketPath.c_str(),0600)!=0 || ::lstat(state->socketPath.c_str(),&existing)!=0 || ::listen(state->listener,32)!=0) {
            ::close(state->listener);::unlink(state->socketPath.c_str());error="Recorder endpoint preparation failed";return Status::IoError;
        }
        state->socketDevice=existing.st_dev;state->socketInode=existing.st_ino;state_=std::move(state);return Status::Ok;
    } catch(...) {error="Recorder service open failed";return Status::IoError;}
}
Status RecorderServiceServer::configure(const RecorderServiceConfig& config,std::string& error) noexcept {
    error.clear();if(!state_)return Status::InvalidArgument;
    try {return configureState(*state_,config,error);}catch(...) {error="Recorder configuration failed";return Status::IoError;}
}
Status RecorderServiceServer::start(std::string& error) noexcept {
    error.clear();if(!state_)return Status::InvalidArgument;
    try {return startState(*state_,error);}catch(...) {error="Recorder start failed";return Status::IoError;}
}
Status RecorderServiceServer::poll(std::uint32_t timeout,std::string& error) noexcept {
    error.clear();if(!state_ || timeout>1000u)return Status::InvalidArgument;
    try {
        auto& state=*state_;reapStop(state);
        std::vector<pollfd> descriptors;descriptors.reserve(state.peers.size()+1u);descriptors.push_back({state.listener,POLLIN,0});
        for(const auto peer:state.peers)descriptors.push_back({peer,POLLIN,0});
        const auto ready=::poll(descriptors.data(),descriptors.size(),static_cast<int>(timeout));
        if(ready<0) {if(errno==EINTR)return Status::Ok;error="Recorder listener poll failed";return Status::IoError;}
        if(ready==0)return Status::Ok;
        std::vector<std::uint8_t> received(kRecorderServiceMaximumMessage);
        for(std::size_t i=descriptors.size();i-->1u;) {
            const auto events=descriptors[i].revents;if(!events)continue;const auto peer=descriptors[i].fd;
            bool keep=(events&(POLLERR|POLLHUP|POLLNVAL))==0;
            if(keep && (events&POLLIN)) {
                const auto count=::recv(peer,received.data(),received.size(),MSG_DONTWAIT|MSG_TRUNC);
                if(count<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))continue;
                detail::Header header{};std::span<const std::uint8_t> payload;
                keep=count>0 && static_cast<std::size_t>(count)<=received.size() &&
                    detail::parsePacket(std::span<const std::uint8_t>{received.data(),static_cast<std::size_t>(count)},header,payload);
                if(keep) {
                    detail::Encoder body;std::string refusal;const auto status=handle(state,header.operation,payload,body,refusal);
                    detail::Encoder output;output.pod(status);output.string(refusal);
                    if(isOk(status))output.bytes.insert(output.bytes.end(),body.bytes.begin(),body.bytes.end());
                    output.valid=output.valid && body.valid;const auto response=detail::packet(header.operation,output);
                    keep=!response.empty() && ::send(peer,response.data(),response.size(),MSG_DONTWAIT|MSG_NOSIGNAL)==static_cast<ssize_t>(response.size());
                }
            }
            if(!keep) {::close(peer);state.peers.erase(state.peers.begin()+static_cast<std::ptrdiff_t>(i-1u));}
        }
        if(descriptors.front().revents&POLLIN)for(unsigned n=0u;n<8u;++n) {
            const int peer=::accept4(state.listener,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);
            if(peer<0)break;
            if(state.peers.size()>=64u || !detail::peerIsOwner(peer)) {::close(peer);continue;}
            int buffer=static_cast<int>(kRecorderServiceMaximumMessage*2u);
            (void)::setsockopt(peer,SOL_SOCKET,SO_SNDBUF,&buffer,sizeof(buffer));(void)::setsockopt(peer,SOL_SOCKET,SO_RCVBUF,&buffer,sizeof(buffer));
            state.peers.push_back(peer);
        }
        return Status::Ok;
    } catch(...) {error="Recorder service control failed";return Status::IoError;}
}
void RecorderServiceServer::requestStop() noexcept {
    if(!state_)return;try {stopState(*state_);}catch(...) {state_->capture.requestStop();}
}
void RecorderServiceServer::close() noexcept {
    if(!state_)return;
    requestStop();if(state_->stopWorker.joinable())state_->stopWorker.join();(void)state_->capture.stop();
    for(const auto peer:state_->peers)::close(peer);if(state_->listener>=0)::close(state_->listener);
    struct stat current{};
    if(::lstat(state_->socketPath.c_str(),&current)==0 && current.st_dev==state_->socketDevice && current.st_ino==state_->socketInode)
        (void)::unlink(state_->socketPath.c_str());
    state_.reset();
}
} // namespace hftrec::service
