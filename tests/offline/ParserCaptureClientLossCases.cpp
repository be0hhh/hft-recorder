#include "OfflineCase.hpp"
#include "Capture/Parser/ParserMarketCaptureClient.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusWriter.hpp"
#include "hft_parser/Ipc/MarketCaptureProtocol.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <new>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {
namespace ipc=hft_parser::ipc;
namespace corpus=hftrec::corpus;
using hftrec::Status;

ipc::MarketCaptureSourceDescriptor source() {
    ipc::MarketCaptureSourceDescriptor value{};
    value.market.sourceId=1;value.market.canonicalSymbolId=7;value.market.sourceGeneration=11;
    value.market.venueId=1;value.market.marketRaw=2;
    value.market.priceScale=value.market.quantityScale=8;
    value.market.economicBaseAssetId=1;value.market.quoteAssetId=2;
    value.market.flags=ipc::MarketDirectoryBookTicker;
    std::copy_n("fixture",7,value.market.venueCode.begin());value.market.venueCodeBytes=7;
    std::copy_n("AAA",3,value.market.canonicalSymbol.begin());value.market.canonicalSymbolBytes=3;
    std::copy_n("AAA",3,value.market.nativeSymbol.begin());value.market.nativeSymbolBytes=3;
    std::copy_n("spot",4,value.marketCode.begin());value.marketCodeBytes=4;
    value.configuredChannelMask=value.availableChannelMask=value.traderReplayChannelMask=1;
    value.compatibility[0]=ipc::MarketCaptureCompatibility::ExactTraderReplay;
    return value;
}

// Synthetic protocol peer only: no Parser runtime, native session or provider.
// The real Recorder consumer attaches a sealed memfd through same-UID SCM_RIGHTS.
class CapturePeer final {
public:
    enum class AttachMode {Valid,ShortWithTwoDescriptors,FullWithTwoDescriptors};
    std::filesystem::path root{};
    ipc::MarketCaptureArenaHeader* header{};
    ipc::MarketCaptureRingHeader* ring{};
    ipc::MarketCaptureRecord* records{};
    ipc::MarketCaptureLossCell* losses{};

    explicit CapturePeer(AttachMode mode=AttachMode::Valid):attachMode_(mode) {
        char temporary[]="/tmp/hftrec-client-loss-XXXXXX";
        const auto* directory=::mkdtemp(temporary);CXET_CHECK(directory);
        root=directory;
        CXET_CHECK(ipc::planMarketCaptureWireLayout(1,1,capacity,&layout_));
        arenaFd_=::memfd_create("recorder-offline-capture",MFD_CLOEXEC|MFD_ALLOW_SEALING);
        CXET_CHECK(arenaFd_>=0 && ::ftruncate(arenaFd_,layout_.arenaBytes)==0);
        mapping_=::mmap(nullptr,layout_.arenaBytes,PROT_READ|PROT_WRITE,MAP_SHARED,arenaFd_,0);
        CXET_CHECK(mapping_!=MAP_FAILED);
        auto* bytes=static_cast<std::byte*>(mapping_);
        header=::new (bytes) ipc::MarketCaptureArenaHeader{};
        header->headerBytes=sizeof(*header);header->arenaBytes=layout_.arenaBytes;header->producerEpoch=13;
        header->directoryCapacity=1;header->shardCount=1;header->channelCount=ipc::kMarketCaptureChannelCount;
        header->recordBytes=sizeof(ipc::MarketCaptureRecord);header->ringCapacity=capacity;
        header->directoryOffset=layout_.directoryOffset;header->lossLedgerOffset=layout_.lossLedgerOffset;
        header->shardDescriptorsOffset=layout_.shardDescriptorsOffset;
        ::new (bytes+layout_.directoryOffset) ipc::MarketCaptureSourceDescriptor{source()};
        losses=reinterpret_cast<ipc::MarketCaptureLossCell*>(bytes+layout_.lossLedgerOffset);
        for(std::size_t i=0;i<ipc::kMarketCaptureChannelCount;++i) ::new (losses+i) ipc::MarketCaptureLossCell{};
        auto* descriptor=::new (bytes+layout_.shardDescriptorsOffset) ipc::MarketCaptureShardDescriptor{};
        descriptor->regionOffset=layout_.shardRegionsOffset;descriptor->regionBytes=layout_.shardRegionBytes;
        auto* shard=::new (bytes+layout_.shardRegionsOffset) ipc::MarketCaptureShardHeader{};
        shard->headerBytes=sizeof(*shard);shard->ringCapacity=capacity;shard->regionBytes=layout_.shardRegionBytes;
        std::size_t offset=layout_.shardRegionsOffset+sizeof(*shard);
        CXET_CHECK(ipc::marketCaptureLayoutAlign(offset,alignof(ipc::MarketCaptureRingHeader),&offset));
        shard->ringHeaderOffset=offset;ring=::new (bytes+offset) ipc::MarketCaptureRingHeader{};
        offset+=sizeof(*ring);
        CXET_CHECK(ipc::marketCaptureLayoutAlign(offset,alignof(ipc::MarketCaptureRecord),&offset));
        shard->recordsOffset=offset;records=reinterpret_cast<ipc::MarketCaptureRecord*>(bytes+offset);
        for(std::size_t i=0;i<capacity;++i) ::new (records+i) ipc::MarketCaptureRecord{};
        header->directoryCount.store(1,std::memory_order_release);
        header->directoryGeneration.store(1,std::memory_order_release);
        CXET_CHECK(::fcntl(arenaFd_,F_ADD_SEALS,F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)==0);
        listener_=::socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);CXET_CHECK(listener_>=0);
        sockaddr_un address{};address.sun_family=AF_UNIX;
        const auto path=(root/ipc::kMarketCaptureSocketName).string();CXET_CHECK(path.size()<sizeof(address.sun_path));
        std::copy(path.begin(),path.end(),address.sun_path);
        CXET_CHECK(::bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
        CXET_CHECK(::listen(listener_,1)==0);
        server_=std::thread([this] {serve();});
    }
    ~CapturePeer() {
        closing_.store(true,std::memory_order_release);
        const auto peer=peerFd_.load(std::memory_order_acquire);
        if(peer>=0) (void)::shutdown(peer,SHUT_RDWR);
        (void)::shutdown(listener_,SHUT_RDWR);
        if(server_.joinable()) server_.join();
        if(listener_>=0) ::close(listener_);
        if(mapping_!=MAP_FAILED) ::munmap(mapping_,layout_.arenaBytes);
        if(arenaFd_>=0) ::close(arenaFd_);
        std::error_code ignored;std::filesystem::remove_all(root,ignored);
    }
    void publish(std::uint64_t sequence) {
        ipc::MarketCaptureRecord row{};auto& h=row.header;
        h.sourceId=1;h.sourceGeneration=11;h.sessionEpoch=17;
        h.eventSequence=h.frameSequence=h.shardSequence=sequence;
        h.exchangeTimestampNs=199+sequence;h.receiveRealtimeNs=200+sequence;h.receiveMonotonicNs=200+sequence;
        h.flags=ipc::MarketCaptureRecordTraderReplayCompatible;
        h.sourceFlags=hft_parser::market::SourceBookTickerBidPresent|hft_parser::market::SourceBookTickerAskPresent;
        h.payloadBytes=sizeof(ipc::MarketCaptureBookTickerPayload);
        *ipc::marketCapturePayload<ipc::MarketCaptureBookTickerPayload>(&row)={100,2,101,3};
        CXET_CHECK(ipc::validMarketCaptureRecord(row));
        const auto write=ring->write.load(std::memory_order_relaxed);
        records[write&(capacity-1u)]=row;ring->write.store(write+1,std::memory_order_release);
    }
    void injectKnownLoss() {
        auto& cell=losses[0];cell.droppedRecords.store(1);cell.gapEpoch.store(1);
        cell.firstDroppedEventSequence.store(3);cell.lastDroppedEventSequence.store(3);
        cell.minimumDroppedReceiveRealtimeNs.store(203);cell.maximumDroppedReceiveRealtimeNs.store(203);
        cell.lastSourceGeneration.store(11);cell.lastShardIndex.store(0);
        header->lossEpoch.store(1,std::memory_order_release);
    }
private:
    static constexpr std::uint32_t capacity=ipc::kMinimumMarketCaptureRingCapacity;
    ipc::MarketCaptureWireLayout layout_{};
    int arenaFd_{-1},listener_{-1};void* mapping_{MAP_FAILED};
    std::atomic<int> peerFd_{-1};std::atomic<bool> closing_{false};
    std::thread server_{};
    const AttachMode attachMode_;
    static bool readable(int descriptor) {
        pollfd item{descriptor,POLLIN,0};return ::poll(&item,1,2000)>0 && (item.revents&POLLIN)!=0;
    }
    static ipc::MarketCaptureControlPacket receive(int descriptor) {
        ipc::MarketCaptureControlPacket packet{};
        if(!readable(descriptor) || ::recv(descriptor,&packet,sizeof(packet),0)!=static_cast<ssize_t>(sizeof(packet)) ||
           !ipc::validMarketCaptureControlHeader(packet.header)) throw std::runtime_error("synthetic capture peer receive failed");
        return packet;
    }
    void serve() noexcept {
        int peer=-1;
        try {
            if(!readable(listener_)) throw std::runtime_error("synthetic capture peer accept timed out");
            peer=::accept4(listener_,nullptr,nullptr,SOCK_CLOEXEC);
            if(peer<0) throw std::runtime_error("synthetic capture peer accept failed");
            peerFd_.store(peer,std::memory_order_release);
            const auto hello=receive(peer);
            if(hello.header.message!=ipc::MarketCaptureControlMessage::Hello) throw std::runtime_error("expected Hello");
            ipc::MarketCaptureControlPacket attach{};attach.header.message=ipc::MarketCaptureControlMessage::Attach;
            attach.header.sequence=hello.header.sequence;attach.header.producerEpoch=13;attach.header.consumerEpoch=hello.header.consumerEpoch;
            auto& payload=*ipc::marketCaptureControlPayload<ipc::MarketCaptureAttach>(&attach);
            payload.arenaBytes=layout_.arenaBytes;payload.directoryGeneration=1;payload.sourceCapacity=1;
            payload.ringCapacity=capacity;payload.shardCount=1;payload.recordBytes=sizeof(ipc::MarketCaptureRecord);
            payload.channelCount=ipc::kMarketCaptureChannelCount;
            const bool malformed=attachMode_!=AttachMode::Valid;
            const auto packetBytes=attachMode_==AttachMode::ShortWithTwoDescriptors?sizeof(attach.header):sizeof(attach);
            const std::array<int,2> attachedDescriptors{arenaFd_,arenaFd_};
            const auto descriptorBytes=sizeof(int)*(malformed?2u:1u);
            iovec vector{&attach,packetBytes};alignas(cmsghdr) std::array<std::byte,CMSG_SPACE(sizeof(int)*2u)> control{};
            msghdr message{};message.msg_iov=&vector;message.msg_iovlen=1;message.msg_control=control.data();message.msg_controllen=control.size();
            message.msg_controllen=CMSG_SPACE(descriptorBytes);
            auto* rights=CMSG_FIRSTHDR(&message);rights->cmsg_level=SOL_SOCKET;rights->cmsg_type=SCM_RIGHTS;
            rights->cmsg_len=CMSG_LEN(descriptorBytes);std::memcpy(CMSG_DATA(rights),attachedDescriptors.data(),descriptorBytes);
            if(::sendmsg(peer,&message,MSG_NOSIGNAL)!=static_cast<ssize_t>(packetBytes)) throw std::runtime_error("Attach send failed");
            if(malformed) {
                while(!closing_.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                peerFd_.store(-1,std::memory_order_release);::close(peer);return;
            }
            const auto ready=receive(peer);
            if(ready.header.message!=ipc::MarketCaptureControlMessage::Ready || ready.header.consumerEpoch!=hello.header.consumerEpoch)
                throw std::runtime_error("expected Ready");
            header->consumerEpoch.store(hello.header.consumerEpoch,std::memory_order_release);
            const auto stop=receive(peer);
            if(stop.header.message!=ipc::MarketCaptureControlMessage::Stop) throw std::runtime_error("expected Stop");
            header->consumerEpoch.store(0,std::memory_order_release);
            ipc::MarketCaptureControlPacket stopped{};stopped.header.message=ipc::MarketCaptureControlMessage::Stopped;
            stopped.header.sequence=stop.header.sequence;stopped.header.producerEpoch=13;stopped.header.consumerEpoch=hello.header.consumerEpoch;
            auto& final=*ipc::marketCaptureControlPayload<ipc::MarketCaptureStopped>(&stopped);
            final.finalLossEpoch=header->lossEpoch.load(std::memory_order_acquire);final.directoryGeneration=1;
            if(::send(peer,&stopped,sizeof(stopped),MSG_NOSIGNAL)!=static_cast<ssize_t>(sizeof(stopped))) throw std::runtime_error("Stopped send failed");
            // Keep the producer control connection alive through final drain.
            while(!closing_.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());}
        peerFd_.store(-1,std::memory_order_release);if(peer>=0) ::close(peer);
    }
};

void connect(hftrec::capture::ParserMarketCaptureClient& client,CapturePeer& peer,
             std::vector<corpus::BinaryMarketSource>& directory) {
    std::string error;const auto status=client.connect(peer.root,error);
    if(status!=Status::Ok) std::fprintf(stderr,"capture connect: %s\n",error.c_str());
    CXET_CHECK(status==Status::Ok);CXET_CHECK(client.loadSourceDirectory(directory,error)==Status::Ok);
    CXET_CHECK(directory.size()==1 && directory[0].sourceId==1 && client.snapshot().connected);
}
std::size_t openDescriptorCount() {
    std::size_t count=0;
    for(auto entry=std::filesystem::directory_iterator("/proc/self/fd");entry!=std::filesystem::directory_iterator{};++entry) ++count;
    return count;
}
void malformedAttachClosesEveryReceivedDescriptor() {
    for(const auto mode:{CapturePeer::AttachMode::ShortWithTwoDescriptors,CapturePeer::AttachMode::FullWithTwoDescriptors}) {
        const auto before=openDescriptorCount();
        {
            CapturePeer peer(mode);hftrec::capture::ParserMarketCaptureClient client;std::string error;
            CXET_CHECK(client.connect(peer.root,error)!=Status::Ok && !error.empty());
            CXET_CHECK(!client.snapshot().connected);
        }
        CXET_CHECK(openDescriptorCount()==before);
    }
}
void knownLossRefusesActiveDrainBeforeSink() {
    CapturePeer peer;hftrec::capture::ParserMarketCaptureClient client;std::vector<corpus::BinaryMarketSource> directory;
    connect(client,peer,directory);peer.publish(1);peer.injectKnownLoss();
    std::uint64_t sinkCalls=0,sourceCalls=0,drained=99;std::string error;
    struct Counts {std::uint64_t* records;std::uint64_t* sources;} counts{&sinkCalls,&sourceCalls};
    const auto status=client.drainTo([](void* p,const corpus::BinaryMarketRecord&) noexcept {
        ++*static_cast<Counts*>(p)->records;return Status::Ok;
    },&counts,16,drained,error,[](void* p,const corpus::BinaryMarketSource&) noexcept {
        ++*static_cast<Counts*>(p)->sources;return Status::Ok;
    });
    CXET_CHECK(status==Status::IoError && !error.empty());
    CXET_CHECK(sinkCalls==0 && sourceCalls==0 && drained==0 && peer.ring->read.load()==0);
    CXET_CHECK(client.snapshot().lossEpoch==1 && !client.producerDisconnected());
    CXET_CHECK(client.stop(error)==Status::Ok);
}
void frozenLossAllowsFinalDrainAndRetainsIncompleteLedger() {
    CapturePeer peer;hftrec::capture::ParserMarketCaptureClient client;std::vector<corpus::BinaryMarketSource> directory;
    connect(client,peer,directory);corpus::BinaryMarketCorpusWriter writer;
    corpus::BinaryMarketWriterConfig config{};config.root=peer.root/"corpus";config.sources=directory;
    config.producerEpoch=13;config.maximumBytes=4*1024*1024;config.segmentTargetBytes=4096;
    config.targetDurationNs=100000;config.startedReceiveNs=100;config.startedMonotonicNs=100;
    config.ringCapacity=ipc::kMinimumMarketCaptureRingCapacity;config.shardCount=1;
    CXET_CHECK(writer.start(config)==Status::Ok);
    peer.publish(1);std::uint64_t drained=0;std::string error;
    CXET_CHECK(client.drain(writer,16,drained,error)==Status::Ok && drained==1);
    peer.publish(2);peer.injectKnownLoss();CXET_CHECK(client.stop(error)==Status::Ok);
    CXET_CHECK(client.snapshot().stopped && !client.producerDisconnected());
    CXET_CHECK(writer.beginFinalDrain()==Status::Ok);
    CXET_CHECK(client.drain(writer,0,drained,error)==Status::Ok && drained==1);
    CXET_CHECK(client.appendFrozenLosses(writer,error)==Status::Ok && client.snapshot().gapsWritten==1);
    CXET_CHECK(writer.finalize(corpus::BinaryMarketStopReason::Error,1000)==Status::Ok);
    corpus::BinaryMarketManifest manifest{};std::ifstream file(config.root/"manifest.bin",std::ios::binary);
    file.read(reinterpret_cast<char*>(&manifest),sizeof(manifest));CXET_CHECK(file.good());
    CXET_CHECK(manifest.complete==0 && manifest.stopReason==corpus::BinaryMarketStopReason::Error);
    CXET_CHECK(manifest.recordCount==2 && manifest.gapCount==1);
}
} // namespace
int main(int argc,char** argv) {
    const cxet::testing::Case cases[]{
        cxet::testing::Case{"capture.malformed_attach_closes_all_received_descriptors",malformedAttachClosesEveryReceivedDescriptor},
        cxet::testing::Case{"capture.known_loss_aborts_active_drain_without_sink",knownLossRefusesActiveDrainBeforeSink},
        cxet::testing::Case{"capture.frozen_loss_final_drain_retains_incomplete_ledger",frozenLossAllowsFinalDrainAndRetainsIncompleteLedger}};
    return cxet::testing::runCases(argc,argv,cases);
}
