#include "ParserMarketCaptureClient.hpp"
#include "Private/CorpusConversion.hpp"

#include "../../Corpus/BinaryMarketCorpusWriter.hpp"
#include "hft_parser/Ipc/MarketCaptureProtocol.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <span>
#include <string_view>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <utility>

namespace hftrec::capture {
namespace parser = hft_parser::ipc;

namespace {

inline constexpr std::uint64_t kHandshakeTimeoutNs = 2'000'000'000u;

struct CaptureShardView final {
    parser::MarketCaptureShardHeader* header{nullptr};
    parser::MarketCaptureRingHeader* ring{nullptr};
    parser::MarketCaptureRecord* records{nullptr};
};

[[nodiscard]] std::uint64_t clockNowNs(clockid_t clock) noexcept {
    timespec value{};
    if (::clock_gettime(clock, &value) != 0 || value.tv_sec < 0 ||
        value.tv_nsec < 0) return 0u;
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000u +
        static_cast<std::uint64_t>(value.tv_nsec);
}

[[nodiscard]] int remainingTimeoutMs(std::uint64_t deadlineNs) noexcept {
    const std::uint64_t now = clockNowNs(CLOCK_MONOTONIC);
    if (now == 0u || now >= deadlineNs) return 0;
    const std::uint64_t remaining = deadlineNs - now;
    const std::uint64_t rounded = (remaining + 999'999u) / 1'000'000u;
    return static_cast<int>(std::min<std::uint64_t>(
        rounded, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
}

[[nodiscard]] bool deadlineElapsed(std::uint64_t deadlineNs) noexcept {
    const auto now=clockNowNs(CLOCK_MONOTONIC);
    return now==0u || now>=deadlineNs;
}

[[nodiscard]] bool waitForFd(int descriptor,
                             short events,
                             std::uint64_t deadlineNs) noexcept {
    bool firstPoll=true;
    while (descriptor >= 0) {
        // Subscription polling gets one zero-timeout attempt. Interrupted
        // retries still obey the original absolute deadline.
        if(!firstPoll && deadlineElapsed(deadlineNs)) return false;
        firstPoll=false;
        pollfd pollDescriptor{descriptor,
                              static_cast<short>(events | POLLERR | POLLHUP),
                              0};
        const int result =
            ::poll(&pollDescriptor, 1u, remainingTimeoutMs(deadlineNs));
        if (result > 0) {
            return (pollDescriptor.revents & events) != 0 &&
                (pollDescriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0;
        }
        if (result == 0) return false;
        if (errno != EINTR) return false;
    }
    return false;
}

[[nodiscard]] bool checkedAdd(std::size_t left,
                              std::size_t right,
                              std::size_t& output) noexcept {
    if (left > std::numeric_limits<std::size_t>::max() - right) return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool checkedMultiply(std::size_t left,
                                   std::size_t right,
                                   std::size_t& output) noexcept {
    if (left != 0u &&
        right > std::numeric_limits<std::size_t>::max() / left) return false;
    output = left * right;
    return true;
}

[[nodiscard]] bool checkedAlign(std::size_t value,
                                std::size_t alignment,
                                std::size_t& output) noexcept {
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u)
        return false;
    const std::size_t mask = alignment - 1u;
    if (!checkedAdd(value, mask, output)) return false;
    output &= ~mask;
    return true;
}

template <typename T>
[[nodiscard]] bool validRegion(std::size_t offset,
                               std::size_t count,
                               std::size_t mappingBytes) noexcept {
    std::size_t bytes = 0u;
    return checkedMultiply(count, sizeof(T), bytes) &&
        offset <= mappingBytes && bytes <= mappingBytes - offset &&
        offset % alignof(T) == 0u;
}

[[nodiscard]] bool advanceShard(std::size_t cursor,
                                std::uint32_t ringCapacity,
                                std::size_t& regionOffset,
                                std::size_t& ringOffset,
                                std::size_t& recordsOffset,
                                std::size_t& regionEnd) noexcept {
    std::size_t recordsBytes = 0u;
    return parser::marketCaptureRingCapacity(ringCapacity) &&
        checkedAlign(cursor, alignof(parser::MarketCaptureShardHeader),
                     regionOffset) &&
        checkedAdd(regionOffset, sizeof(parser::MarketCaptureShardHeader),
                   cursor) &&
        checkedAlign(cursor, alignof(parser::MarketCaptureRingHeader),
                     ringOffset) &&
        checkedAdd(ringOffset, sizeof(parser::MarketCaptureRingHeader),
                   cursor) &&
        checkedAlign(cursor, alignof(parser::MarketCaptureRecord),
                     recordsOffset) &&
        checkedMultiply(ringCapacity, sizeof(parser::MarketCaptureRecord),
                        recordsBytes) &&
        checkedAdd(recordsOffset, recordsBytes, regionEnd);
}

[[nodiscard]] bool sendControlPacket(int descriptor,
                                     const parser::MarketCaptureControlPacket& packet,
                                     std::uint64_t deadlineNs) noexcept {
    while (descriptor >= 0) {
        if(deadlineElapsed(deadlineNs)) return false;
        const ssize_t sent = ::send(descriptor, &packet, sizeof(packet),
                                    MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent == static_cast<ssize_t>(sizeof(packet))) return true;
        if (sent >= 0) return false;
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (!waitForFd(descriptor, POLLOUT, deadlineNs)) return false;
    }
    return false;
}

struct ReceivedControl final {
    parser::MarketCaptureControlPacket packet{};
    int attachedFd{-1};
};

void closeAttached(ReceivedControl& received) noexcept {
    if (received.attachedFd >= 0) ::close(received.attachedFd);
    received.attachedFd = -1;
}

[[nodiscard]] bool receiveControlPacket(int descriptor,
                                        std::uint64_t deadlineNs,
                                        ReceivedControl& output) noexcept {
    output = {};
    output.attachedFd = -1;
    bool firstReceive=true;
    while (descriptor >= 0) {
        if(!firstReceive && deadlineElapsed(deadlineNs)) return false;
        firstReceive=false;
        if (!waitForFd(descriptor, POLLIN, deadlineNs)) return false;
        iovec vector{&output.packet, sizeof(output.packet)};
        alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(int) * 2u)> control{};
        msghdr message{};
        message.msg_iov = &vector;
        message.msg_iovlen = 1u;
        message.msg_control = control.data();
        message.msg_controllen = control.size();
        const ssize_t received = ::recvmsg(
            descriptor, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return false;
        }
        // recvmsg installs SCM_RIGHTS descriptors even when the data packet
        // is short/truncated. Visit every delivered descriptor before refusal.
        std::size_t descriptorCount = 0u;
        bool validAncillary=true;
        for (cmsghdr* current = CMSG_FIRSTHDR(&message); current;
             current = CMSG_NXTHDR(&message, current)) {
            const auto offset=static_cast<std::size_t>(
                reinterpret_cast<const std::byte*>(current)-control.data());
            if(offset>message.msg_controllen || current->cmsg_len<CMSG_LEN(0u) ||
               current->cmsg_len>message.msg_controllen-offset) {
                validAncillary=false;break;
            }
            if (current->cmsg_level != SOL_SOCKET ||
                current->cmsg_type != SCM_RIGHTS) {
                validAncillary=false;continue;
            }
            const auto bytes=current->cmsg_len-CMSG_LEN(0u);
            if(bytes!=sizeof(int)) validAncillary=false;
            for(std::size_t position=0u;position+sizeof(int)<=bytes;position+=sizeof(int)) {
                int receivedFd=-1;
                std::memcpy(&receivedFd,CMSG_DATA(current)+position,sizeof(receivedFd));
                ++descriptorCount;
                if(descriptorCount==1u) output.attachedFd=receivedFd;
                else if(receivedFd>=0) ::close(receivedFd);
            }
        }
        if (!validAncillary || descriptorCount > 1u ||
            received != static_cast<ssize_t>(sizeof(output.packet)) ||
            (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
            closeAttached(output);
            return false;
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool validControlReply(
    const parser::MarketCaptureControlPacket& packet,
    parser::MarketCaptureControlMessage message,
    std::uint64_t sequence,
    std::uint64_t producerEpoch,
    std::uint64_t consumerEpoch) noexcept {
    return parser::validMarketCaptureControlHeader(packet.header) &&
        packet.header.message == message && packet.header.sequence == sequence &&
        packet.header.producerEpoch == producerEpoch &&
        packet.header.consumerEpoch == consumerEpoch;
}

[[nodiscard]] bool sameUidRuntimeSocket(
    const std::filesystem::path& runtimeDirectory,
    std::array<char, sizeof(sockaddr_un::sun_path)>& socketPath,
    std::string& error) {
    const std::string directory = runtimeDirectory.string();
    if (directory.empty() || directory.front() != '/' ||
        directory.find('\0') != std::string::npos) {
        error = "parser capture runtime directory must be an absolute path";
        return false;
    }
    struct stat state {};
    if (::lstat(directory.c_str(), &state) != 0 || !S_ISDIR(state.st_mode) ||
        state.st_uid != ::geteuid() || (state.st_mode & 0077) != 0u) {
        error = "parser capture runtime directory must be owner-only (0700)";
        return false;
    }
    const std::string_view socketName{parser::kMarketCaptureSocketName};
    const bool slash = directory.back() != '/';
    if (directory.size() + (slash ? 1u : 0u) + socketName.size() + 1u >
        socketPath.size()) {
        error = "parser capture socket path is too long";
        return false;
    }
    socketPath.fill('\0');
    std::copy(directory.begin(), directory.end(), socketPath.begin());
    std::size_t offset = directory.size();
    if (slash) socketPath[offset++] = '/';
    std::copy(socketName.begin(), socketName.end(),
              socketPath.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

}  // namespace

struct ParserMarketCaptureClientState final {
    int socketFd{-1};
    void* mapping{nullptr};
    std::size_t mappingBytes{0u};
    parser::MarketCaptureArenaHeader* header{nullptr};
    parser::MarketCaptureSourceDescriptor* sourceDirectory{nullptr};
    parser::MarketCaptureLossCell* lossLedger{nullptr};
    std::vector<CaptureShardView> shards{};
    std::vector<parser::MarketCaptureSourceDescriptor> sources{};
    std::uint64_t consumerEpoch{0u};
    std::uint64_t directoryGeneration{0u};
    std::uint64_t nextControlSequence{3u};
    std::uint64_t pendingControlSequence{0u};
    std::uint64_t pendingControlDeadline{0u};
    parser::MarketCaptureSubscriptionChange pendingChange{};
    std::size_t registeredSourceCount{0u};
    std::uint64_t recordsDrained{0u};
    std::uint64_t gapsWritten{0u};
    std::uint64_t finalLossEpoch{0u};
    std::int64_t captureStartedReceiveNs{0};
    std::uint64_t captureStartedMonotonicNs{0u};
    bool stopped{false};
    bool lossesAppended{false};

    ~ParserMarketCaptureClientState() noexcept {
        if (mapping && mapping != MAP_FAILED)
            (void)::munmap(mapping, mappingBytes);
        if (socketFd >= 0) ::close(socketFd);
    }
};

namespace {

[[nodiscard]] bool bindArena(
    ParserMarketCaptureClientState& state,
    const parser::MarketCaptureAttach& attach,
    std::string& error) noexcept {
    if (!state.mapping || state.mapping == MAP_FAILED ||
        state.mappingBytes < sizeof(parser::MarketCaptureArenaHeader)) {
        error = "parser capture arena mapping is invalid";
        return false;
    }
    auto* const bytes = static_cast<std::byte*>(state.mapping);
    auto* const header =
        reinterpret_cast<parser::MarketCaptureArenaHeader*>(bytes);
    parser::MarketCaptureWireLayout layout{};
    if (!parser::planMarketCaptureWireLayout(
            attach.sourceCapacity, attach.shardCount, attach.ringCapacity,
            &layout) || layout.arenaBytes != state.mappingBytes ||
        header->magic != parser::kMarketCaptureArenaMagic ||
        header->version != parser::kMarketCaptureArenaVersion ||
        header->headerBytes != sizeof(*header) ||
        header->arenaBytes != state.mappingBytes ||
        header->producerEpoch == 0u ||
        header->directoryCapacity != attach.sourceCapacity ||
        header->shardCount != attach.shardCount ||
        header->channelCount != parser::kMarketCaptureChannelCount ||
        header->recordBytes != sizeof(parser::MarketCaptureRecord) ||
        header->ringCapacity != attach.ringCapacity ||
        header->directoryOffset != layout.directoryOffset ||
        header->lossLedgerOffset != layout.lossLedgerOffset ||
        header->shardDescriptorsOffset != layout.shardDescriptorsOffset ||
        header->reserved32 != 0u ||
        header->reserved != decltype(header->reserved){} ||
        !validRegion<parser::MarketCaptureSourceDescriptor>(
            layout.directoryOffset, attach.sourceCapacity,
            state.mappingBytes) ||
        !validRegion<parser::MarketCaptureLossCell>(
            layout.lossLedgerOffset,
            static_cast<std::size_t>(attach.sourceCapacity) *
                parser::kMarketCaptureChannelCount,
            state.mappingBytes) ||
        !validRegion<parser::MarketCaptureShardDescriptor>(
            layout.shardDescriptorsOffset, attach.shardCount,
            state.mappingBytes)) {
        error = "parser capture arena ABI/layout mismatch";
        return false;
    }
    auto* const descriptors =
        reinterpret_cast<parser::MarketCaptureShardDescriptor*>(
            bytes + layout.shardDescriptorsOffset);
    try {
        state.shards.resize(attach.shardCount);
    } catch (...) {
        error = "parser capture shard view allocation failed";
        return false;
    }
    std::size_t cursor = layout.shardRegionsOffset;
    for (std::uint16_t index = 0u; index < attach.shardCount; ++index) {
        std::size_t regionOffset = 0u;
        std::size_t ringOffset = 0u;
        std::size_t recordsOffset = 0u;
        std::size_t regionEnd = 0u;
        if (!advanceShard(cursor, attach.ringCapacity, regionOffset,
                          ringOffset, recordsOffset, regionEnd) ||
            descriptors[index].regionOffset != regionOffset ||
            descriptors[index].regionBytes != regionEnd - regionOffset ||
            !validRegion<parser::MarketCaptureShardHeader>(
                regionOffset, 1u, state.mappingBytes) ||
            !validRegion<parser::MarketCaptureRingHeader>(
                ringOffset, 1u, state.mappingBytes) ||
            !validRegion<parser::MarketCaptureRecord>(
                recordsOffset, attach.ringCapacity, state.mappingBytes)) {
            error = "parser capture shard layout mismatch";
            return false;
        }
        auto* const shard =
            reinterpret_cast<parser::MarketCaptureShardHeader*>(
                bytes + regionOffset);
        if (shard->shardIndex != index ||
            shard->headerBytes != sizeof(*shard) ||
            shard->ringCapacity != attach.ringCapacity ||
            shard->regionBytes != descriptors[index].regionBytes ||
            shard->ringHeaderOffset != ringOffset ||
            shard->recordsOffset != recordsOffset ||
            shard->inFlightPublishes.load(std::memory_order_relaxed) != 0u ||
            shard->reserved != decltype(shard->reserved){}) {
            error = "parser capture shard header mismatch";
            return false;
        }
        state.shards[index] = {
            shard,
            reinterpret_cast<parser::MarketCaptureRingHeader*>(
                bytes + ringOffset),
            reinterpret_cast<parser::MarketCaptureRecord*>(
                bytes + recordsOffset)};
        cursor = regionEnd;
    }
    if (cursor != state.mappingBytes) {
        error = "parser capture arena has trailing or missing bytes";
        return false;
    }
    state.header = header;
    state.sourceDirectory =
        reinterpret_cast<parser::MarketCaptureSourceDescriptor*>(
            bytes + layout.directoryOffset);
    state.lossLedger = reinterpret_cast<parser::MarketCaptureLossCell*>(
        bytes + layout.lossLedgerOffset);
    return true;
}

[[nodiscard]] bool socketPeerDisconnected(int descriptor) noexcept {
    if (descriptor < 0) return true;
    pollfd value{descriptor,
                 static_cast<short>(POLLIN | POLLERR | POLLHUP), 0};
    const int result = ::poll(&value, 1u, 0);
    return result < 0 ||
        (result > 0 &&
         (value.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0);
}

}  // namespace

ParserMarketCaptureClient::ParserMarketCaptureClient() noexcept = default;

ParserMarketCaptureClient::~ParserMarketCaptureClient() noexcept {
    disconnect();
}

Status ParserMarketCaptureClient::connect(
    const std::filesystem::path& runtimeDirectory,
    std::string& error) noexcept {
    disconnect();
    error.clear();
    try {
        std::array<char, sizeof(sockaddr_un::sun_path)> socketPath{};
        if (!sameUidRuntimeSocket(runtimeDirectory, socketPath, error))
            return Status::InvalidArgument;
        auto state = std::make_unique<ParserMarketCaptureClientState>();
        state->socketFd =
            ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (state->socketFd < 0) {
            error = "failed to create parser capture control socket";
            return Status::IoError;
        }
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::copy(socketPath.begin(), socketPath.end(), address.sun_path);
        const std::uint64_t deadline =
            clockNowNs(CLOCK_MONOTONIC) + kHandshakeTimeoutNs;
        if (::connect(state->socketFd,
                      reinterpret_cast<const sockaddr*>(&address),
                      sizeof(address)) != 0) {
            if (errno != EINPROGRESS ||
                !waitForFd(state->socketFd, POLLOUT, deadline)) {
                error = "parserd market-capture socket is unavailable";
                return Status::IoError;
            }
            int socketError = 0;
            socklen_t socketErrorBytes = sizeof(socketError);
            if (::getsockopt(state->socketFd, SOL_SOCKET, SO_ERROR,
                             &socketError, &socketErrorBytes) != 0 ||
                socketErrorBytes != sizeof(socketError) || socketError != 0) {
                error = "parserd market-capture socket connection failed";
                return Status::IoError;
            }
        }
        ucred peer{};
        socklen_t peerBytes = sizeof(peer);
        if (::getsockopt(state->socketFd, SOL_SOCKET, SO_PEERCRED,
                         &peer, &peerBytes) != 0 || peerBytes != sizeof(peer) ||
            peer.uid != ::geteuid()) {
            error = "parserd market-capture peer UID mismatch";
            return Status::CorruptData;
        }
        const std::uint64_t monotonic = clockNowNs(CLOCK_MONOTONIC);
        const std::uint64_t realtime = clockNowNs(CLOCK_REALTIME);
        state->consumerEpoch = monotonic ^ (realtime << 1u) ^
            (static_cast<std::uint64_t>(::getpid()) << 32u);
        if (state->consumerEpoch == 0u) state->consumerEpoch = 1u;

        parser::MarketCaptureControlPacket hello{};
        hello.header.message = parser::MarketCaptureControlMessage::Hello;
        hello.header.sequence = 1u;
        hello.header.consumerEpoch = state->consumerEpoch;
        auto* const helloPayload =
            parser::marketCaptureControlPayload<parser::MarketCaptureHello>(
                &hello);
        helloPayload->processId = static_cast<std::uint32_t>(::getpid());
        helloPayload->userId = static_cast<std::uint32_t>(::geteuid());
        if (!sendControlPacket(state->socketFd, hello, deadline)) {
            error = "parser capture Hello could not be sent";
            return Status::IoError;
        }

        ReceivedControl received{};
        if (!receiveControlPacket(state->socketFd, deadline, received)) {
            error = "parser capture Attach timed out";
            return Status::IoError;
        }
        if (received.packet.header.message ==
            parser::MarketCaptureControlMessage::Reject) {
            closeAttached(received);
            error = "parserd rejected market-capture attachment";
            return Status::CorruptData;
        }
        if (!validControlReply(
                received.packet, parser::MarketCaptureControlMessage::Attach,
                1u, received.packet.header.producerEpoch,
                state->consumerEpoch) ||
            received.packet.header.producerEpoch == 0u ||
            received.attachedFd < 0) {
            closeAttached(received);
            error = "parser capture Attach contract mismatch";
            return Status::CorruptData;
        }
        const auto& attach = *parser::marketCaptureControlPayload<
            parser::MarketCaptureAttach>(&received.packet);
        if (attach.arenaBytes == 0u ||
            attach.arenaBytes > std::numeric_limits<std::size_t>::max() ||
            attach.sourceCapacity == 0u || attach.shardCount == 0u ||
            attach.recordBytes != sizeof(parser::MarketCaptureRecord) ||
            attach.channelCount != parser::kMarketCaptureChannelCount ||
            attach.reserved != decltype(attach.reserved){}) {
            closeAttached(received);
            error = "parser capture Attach payload mismatch";
            return Status::CorruptData;
        }
        struct stat arenaState {};
        const int seals = ::fcntl(received.attachedFd, F_GET_SEALS);
        const int requiredSeals = F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
        if (::fstat(received.attachedFd, &arenaState) != 0 ||
            arenaState.st_size < 0 ||
            static_cast<std::uint64_t>(arenaState.st_size) !=
                attach.arenaBytes ||
            seals < 0 || (seals & requiredSeals) != requiredSeals) {
            closeAttached(received);
            error = "parser capture arena descriptor is not sealed/complete";
            return Status::CorruptData;
        }
        state->mappingBytes = static_cast<std::size_t>(attach.arenaBytes);
        state->mapping = ::mmap(nullptr, state->mappingBytes,
                                PROT_READ | PROT_WRITE, MAP_SHARED,
                                received.attachedFd, 0);
        closeAttached(received);
        if (state->mapping == MAP_FAILED) {
            state->mapping = nullptr;
            error = "parser capture arena mapping failed";
            return Status::IoError;
        }
        if (!bindArena(*state, attach, error) ||
            state->header->producerEpoch !=
                received.packet.header.producerEpoch) {
            if (error.empty()) error = "parser capture producer epoch mismatch";
            return Status::CorruptData;
        }

        parser::MarketCaptureControlPacket ready{};
        ready.header.message = parser::MarketCaptureControlMessage::Ready;
        ready.header.sequence = 2u;
        ready.header.producerEpoch = state->header->producerEpoch;
        ready.header.consumerEpoch = state->consumerEpoch;
        auto* const readyPayload =
            parser::marketCaptureControlPayload<parser::MarketCaptureReady>(
                &ready);
        readyPayload->mappedArenaBytes = state->mappingBytes;
        const std::uint64_t readyMonotonicNs =
            clockNowNs(CLOCK_MONOTONIC);
        const std::uint64_t readyReceiveNs = clockNowNs(CLOCK_REALTIME);
        if (readyMonotonicNs == 0u || readyReceiveNs == 0u ||
            readyReceiveNs > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            error = "parser capture Ready timestamp is unavailable";
            return Status::IoError;
        }
        state->captureStartedMonotonicNs = readyMonotonicNs;
        state->captureStartedReceiveNs =
            static_cast<std::int64_t>(readyReceiveNs);
        if (!sendControlPacket(state->socketFd, ready, deadline)) {
            error = "parser capture Ready could not be sent";
            return Status::IoError;
        }
        while (state->header->consumerEpoch.load(std::memory_order_acquire) !=
               state->consumerEpoch) {
            if (socketPeerDisconnected(state->socketFd) ||
                clockNowNs(CLOCK_MONOTONIC) >= deadline) {
                error = "parser capture Ready was not accepted";
                return Status::CorruptData;
            }
            std::this_thread::yield();
        }
        state_ = std::move(state);
        return Status::Ok;
    } catch (...) {
        error = "parser capture connection setup failed";
        return Status::IoError;
    }
}

Status ParserMarketCaptureClient::loadSourceDirectory(
    std::vector<corpus::BinaryMarketSource>& sources,
    std::string& error) noexcept {
    sources.clear();
    error.clear();
    if (!state_ || !state_->header) {
        error = "parser capture client is not connected";
        return Status::InvalidArgument;
    }
    try {
        const std::uint64_t deadline =
            clockNowNs(CLOCK_MONOTONIC) + kHandshakeTimeoutNs;
        while (clockNowNs(CLOCK_MONOTONIC) < deadline) {
            const std::uint64_t generation =
                state_->header->directoryGeneration.load(
                    std::memory_order_acquire);
            const std::uint32_t count = state_->header->directoryCount.load(
                std::memory_order_acquire);
            if (generation == 0u || count == 0u ||
                count > state_->header->directoryCapacity) {
                if (producerDisconnected()) {
                    error = "parserd disconnected before publishing capture directory";
                    return Status::IoError;
                }
                std::this_thread::yield();
                continue;
            }
            std::vector<parser::MarketCaptureSourceDescriptor> copied(
                state_->sourceDirectory, state_->sourceDirectory + count);
            const std::uint64_t confirmed =
                state_->header->directoryGeneration.load(
                    std::memory_order_acquire);
            if (confirmed == 0u || confirmed != generation ||
                state_->header->directoryCount.load(
                    std::memory_order_acquire) != count) {
                std::this_thread::yield();
                continue;
            }
            const std::uint64_t captureMonotonicNs =
                clockNowNs(CLOCK_MONOTONIC);
            const std::uint64_t captureReceiveNs =
                clockNowNs(CLOCK_REALTIME);
            if (captureMonotonicNs == 0u || captureReceiveNs == 0u ||
                captureReceiveNs > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                error = "parser capture activation timestamp is unavailable";
                return Status::IoError;
            }
            sources.reserve(count);
            for (std::size_t index = 0u; index < copied.size(); ++index) {
                if (!detail::validSourceDescriptor(copied[index], index)) {
                    error = "parser capture source directory is invalid";
                    return Status::CorruptData;
                }
                corpus::BinaryMarketSource source =
                    detail::convertSource(copied[index]);
                if (!corpus::validBinaryMarketSource(source, index)) {
                    error = "parser capture source directory cannot be persisted";
                    return Status::CorruptData;
                }
                sources.push_back(std::move(source));
            }
            const bool initial = state_->sources.empty();
            if (copied.size() < state_->sources.size()) {
                error = "parser capture source IDs were retired instead of remaining stable";
                return Status::CorruptData;
            }
            if (initial) {
                state_->sources = copied;
                state_->registeredSourceCount = copied.size();
            } else {
                for (std::size_t index=state_->sources.size();index<copied.size();++index) {
                    auto baseline=copied[index];
                    baseline.market.sourceGeneration=0u;
                    baseline.availableChannelMask=baseline.traderReplayChannelMask=0u;
                    baseline.compatibility={};
                    state_->sources.push_back(baseline);
                }
            }
            state_->directoryGeneration = generation;
            // Ready only attaches the arena. A capture interval starts after
            // the complete cross-lane directory is stable; records committed
            // between directory publication and this sample remain in the
            // ring and the writer moves its replay anchor to their first
            // monotonic coordinate.
            if (initial) {
                state_->captureStartedMonotonicNs = captureMonotonicNs;
                state_->captureStartedReceiveNs = static_cast<std::int64_t>(captureReceiveNs);
            }
            return Status::Ok;
        }
        error = "parser capture source directory did not become ready";
        return Status::IoError;
    } catch (...) {
        error = "parser capture source directory allocation failed";
        return Status::IoError;
    }
}

Status ParserMarketCaptureClient::drain(
    corpus::BinaryMarketCorpusWriter& writer,
    std::uint64_t maximumRecords,
    std::uint64_t& drained,
    std::string& error) noexcept {
    return drainTo([](void* context,const corpus::BinaryMarketRecord& record) noexcept {
        return static_cast<corpus::BinaryMarketCorpusWriter*>(context)->append(record);
    },&writer,maximumRecords,drained,error,
    [](void* context,const corpus::BinaryMarketSource& source) noexcept {
        return static_cast<corpus::BinaryMarketCorpusWriter*>(context)->registerSources({&source,1});
    });
}

Status ParserMarketCaptureClient::drainTo(ParserCaptureRecordSink sink,void* context,
    std::uint64_t maximumRecords,std::uint64_t& drained,std::string& error,
    ParserCaptureSourceSink sourceSink) noexcept {
    drained = 0u;
    error.clear();
    if (!sink || !state_ || !state_->header || state_->sources.empty()) {
        error = "parser capture directory was not loaded";
        return Status::InvalidArgument;
    }
    if (state_->header->producerEpoch == 0u) {
        error = "parser capture producer epoch is unavailable";
        return Status::CorruptData;
    }
    if (!state_->stopped && state_->header->lossEpoch.load(std::memory_order_acquire) != 0u) {
        error = "parser capture lost records; freeze capture and retain the loss ledger";
        return Status::IoError;
    }
    if (state_->header->directoryGeneration.load(std::memory_order_acquire) != state_->directoryGeneration ||
        state_->header->directoryCount.load(std::memory_order_acquire) != state_->sources.size()) {
        std::vector<corpus::BinaryMarketSource> current;
        const auto status=loadSourceDirectory(current,error);
        if (!isOk(status)) return status;
    }
    while (state_->registeredSourceCount < state_->sources.size()) {
        if (!sourceSink) { error="capture sink has no source catalog consumer";return Status::Unimplemented; }
        auto source=detail::convertSource(state_->sources[state_->registeredSourceCount]);
        source.initiallyPresent=0u;
        const auto status=sourceSink(context,source);
        if (!isOk(status)) { error="capture source catalog registration failed";return status; }
        ++state_->registeredSourceCount;
    }
    const std::uint64_t limit = maximumRecords == 0u
        ? std::numeric_limits<std::uint64_t>::max() : maximumRecords;
    while (drained < limit) {
        std::uint16_t shardIndex=static_cast<std::uint16_t>(state_->shards.size());
        std::uint64_t firstArrival=UINT64_MAX;
        for (std::uint16_t candidate=0;candidate<state_->shards.size();++candidate) {
            const auto& view=state_->shards[candidate];
            const auto read=view.ring->read.load(std::memory_order_relaxed);
            const auto write=view.ring->write.load(std::memory_order_acquire);
            if (read>write || write-read>view.header->ringCapacity) {
                error="parser capture ring cursor invariant failed";return Status::CorruptData;
            }
            if (read==write) continue;
            const auto arrival=view.records[read&(view.header->ringCapacity-1u)].header.receiveMonotonicNs;
            if (shardIndex==state_->shards.size() || arrival<firstArrival) {
                firstArrival=arrival;shardIndex=candidate;
            }
        }
        if (shardIndex==state_->shards.size()) break;
        auto& shard=state_->shards[shardIndex];
        auto read=shard.ring->read.load(std::memory_order_relaxed);
        const auto capacity=shard.header->ringCapacity;
            const parser::MarketCaptureRecord input =
                shard.records[read & (capacity - 1u)];
            // The producer commits the membership marker before releasing its
            // directory revision. Defer this head until that publication is visible.
            if (input.header.sourceId>state_->sources.size() ||
                (input.header.channel==parser::MarketCaptureChannel::Membership &&
                 parser::marketCapturePayload<parser::MarketCaptureMembershipPayload>(&input)->directoryRevision>
                    state_->directoryGeneration)) {
                if (input.header.sourceId>state_->header->directoryCapacity) {
                    error="capture source exceeds admitted directory capacity";return Status::CorruptData;
                }
                break;
            }
            corpus::BinaryMarketRecord output{};
            if (!detail::convertRecord(input, shardIndex, state_->sources, output)) {
                error = "parser capture record contract mismatch";
                return Status::CorruptData;
            }
            const Status status = sink(context,output);
            if (!isOk(status)) {
                error = status == Status::OutOfRange
                    ? "binary market corpus byte quota reached"
                    : "binary market corpus record write failed";
                return status;
            }
            if (input.header.channel==parser::MarketCaptureChannel::Membership) {
                state_->sources[input.header.sourceId-1u]=
                    parser::marketCapturePayload<parser::MarketCaptureMembershipPayload>(&input)->source;
            }
            ++read;
            shard.ring->read.store(read, std::memory_order_release);
            ++drained;
            ++state_->recordsDrained;
    }
    return Status::Ok;
}

Status ParserMarketCaptureClient::stop(std::string& error) noexcept {
    error.clear();
    if (!state_ || !state_->header || state_->stopped) {
        if (state_ && state_->stopped) return Status::Ok;
        error = "parser capture client is not connected";
        return Status::InvalidArgument;
    }
    if (producerDisconnected()) {
        error = "parserd disconnected before capture Stop acknowledgement";
        return Status::IoError;
    }
    if (state_->pendingControlSequence != 0u) {
        error="subscription command is still pending before Stop";
        return Status::InvalidArgument;
    }
    const std::uint64_t deadline =
        clockNowNs(CLOCK_MONOTONIC) + kHandshakeTimeoutNs;
    parser::MarketCaptureControlPacket stop{};
    stop.header.message = parser::MarketCaptureControlMessage::Stop;
    stop.header.sequence = state_->nextControlSequence++;
    stop.header.producerEpoch = state_->header->producerEpoch;
    stop.header.consumerEpoch = state_->consumerEpoch;
    if (!sendControlPacket(state_->socketFd, stop, deadline)) {
        error = "parser capture Stop could not be sent";
        return Status::IoError;
    }
    ReceivedControl received{};
    if (!receiveControlPacket(state_->socketFd, deadline, received)) {
        error = "parser capture Stopped acknowledgement timed out";
        return Status::IoError;
    }
    const bool hasUnexpectedFd = received.attachedFd >= 0;
    closeAttached(received);
    if (hasUnexpectedFd || !validControlReply(
            received.packet, parser::MarketCaptureControlMessage::Stopped,
            stop.header.sequence, state_->header->producerEpoch, state_->consumerEpoch)) {
        error = "parser capture Stopped contract mismatch";
        return Status::CorruptData;
    }
    const auto& stopped = *parser::marketCaptureControlPayload<
        parser::MarketCaptureStopped>(&received.packet);
    if (stopped.directoryGeneration == 0u ||
        stopped.reserved != decltype(stopped.reserved){} ||
        state_->header->consumerEpoch.load(std::memory_order_acquire) != 0u) {
        error = "parser capture producer did not freeze cleanly";
        return Status::CorruptData;
    }
    state_->finalLossEpoch = stopped.finalLossEpoch;
    state_->directoryGeneration = stopped.directoryGeneration;
    state_->stopped = true;
    return Status::Ok;
}

Status ParserMarketCaptureClient::beginSubscriptionChange(std::uint32_t sourceId,
    corpus::BinaryMarketChannel channel,bool add,std::string& error) noexcept {
    error.clear();
    if (!state_ || !state_->header || state_->stopped || state_->pendingControlSequence ||
        sourceId==0u || sourceId>state_->sources.size() ||
        channel<corpus::BinaryMarketChannel::BookTicker || channel>corpus::BinaryMarketChannel::Depth ||
        state_->nextControlSequence==UINT64_MAX) {
        error="invalid or busy capture subscription command";return Status::InvalidArgument;
    }
    std::vector<corpus::BinaryMarketSource> current;
    auto status=loadSourceDirectory(current,error);
    if (!isOk(status)) return status;
    const auto& source=current[sourceId-1u];
    parser::MarketCaptureControlPacket packet{};
    packet.header.message=parser::MarketCaptureControlMessage::ChangeSubscription;
    packet.header.sequence=state_->nextControlSequence;
    packet.header.producerEpoch=state_->header->producerEpoch;
    packet.header.consumerEpoch=state_->consumerEpoch;
    auto& change=*parser::marketCaptureControlPayload<parser::MarketCaptureSubscriptionChange>(&packet);
    change.expectedDirectoryRevision=state_->directoryGeneration;
    change.sourceId=sourceId;change.canonicalSymbolId=source.canonicalSymbolId;
    change.venueId=source.venueId;change.marketRaw=source.marketRaw;
    change.channel=static_cast<parser::MarketCaptureChannel>(channel);
    change.action=add?parser::MarketCaptureSubscriptionAction::Add:parser::MarketCaptureSubscriptionAction::Remove;
    if (!parser::validMarketCaptureSubscriptionChange(change)) {
        error="capture source has no exact command identity";return Status::CorruptData;
    }
    const auto sent=::send(state_->socketFd,&packet,sizeof(packet),MSG_DONTWAIT|MSG_NOSIGNAL);
    if (sent!=static_cast<ssize_t>(sizeof(packet))) {
        error="capture subscription command was not accepted by the local socket";return Status::IoError;
    }
    state_->pendingChange=change;
    state_->pendingControlSequence=state_->nextControlSequence++;
    state_->pendingControlDeadline=clockNowNs(CLOCK_MONOTONIC)+kHandshakeTimeoutNs;
    return Status::Ok;
}

Status ParserMarketCaptureClient::pollSubscriptionChange(bool& complete,Status& outcome,
    std::string& error) noexcept {
    complete=false;outcome=Status::Unknown;error.clear();
    if (!state_ || !state_->pendingControlSequence) {
        error="no pending capture subscription command";return Status::InvalidArgument;
    }
    ReceivedControl received{};
    // Zero timeout consumes only an already available SOCK_SEQPACKET reply.
    if (!receiveControlPacket(state_->socketFd,clockNowNs(CLOCK_MONOTONIC),received)) {
        if (producerDisconnected() || clockNowNs(CLOCK_MONOTONIC)>=state_->pendingControlDeadline) {
            error="capture subscription outcome is unknown after disconnect or timeout";return Status::IoError;
        }
        return Status::Ok;
    }
    const bool unexpectedFd=received.attachedFd>=0;closeAttached(received);
    if (unexpectedFd || !validControlReply(received.packet,parser::MarketCaptureControlMessage::SubscriptionResult,
        state_->pendingControlSequence,state_->header->producerEpoch,state_->consumerEpoch)) {
        error="capture subscription reply contract mismatch";return Status::CorruptData;
    }
    const auto& result=*parser::marketCaptureControlPayload<parser::MarketCaptureSubscriptionResult>(&received.packet);
    const auto& expected=state_->pendingChange;
    if (result.sourceId!=expected.sourceId || result.canonicalSymbolId!=expected.canonicalSymbolId ||
        result.channel!=expected.channel || result.action!=expected.action ||
        result.status>parser::MarketCaptureSubscriptionStatus::LifecycleFailed || result.reserved!=decltype(result.reserved){} ||
        (result.availableChannelMask&~parser::kMarketCaptureNativeChannelMask)!=0u ||
        (result.status==parser::MarketCaptureSubscriptionStatus::Applied &&
         (!result.sourceGeneration || !result.sessionEpoch || !result.markerShardSequence ||
          result.effectiveRealtimeNs<=0 || !result.effectiveMonotonicNs || result.shardIndex>=state_->shards.size()))) {
        error="capture subscription result identity or evidence mismatch";return Status::CorruptData;
    }
    complete=true;state_->pendingControlSequence=0u;
    switch (result.status) {
        case parser::MarketCaptureSubscriptionStatus::Applied:
        case parser::MarketCaptureSubscriptionStatus::NoChange: outcome=Status::Ok;break;
        case parser::MarketCaptureSubscriptionStatus::UnsupportedChannel:
        case parser::MarketCaptureSubscriptionStatus::UnsupportedSharedSession: outcome=Status::Unimplemented;break;
        case parser::MarketCaptureSubscriptionStatus::CapacityExceeded: outcome=Status::OutOfRange;break;
        default: outcome=Status::InvalidArgument;break;
    }
    if (!isOk(outcome)) error="Parser refused subscription change, status="+std::to_string(static_cast<unsigned>(result.status));
    return Status::Ok;
}

Status ParserMarketCaptureClient::freezeDisconnected(
    std::string& error) noexcept {
    error.clear();
    if (!state_ || !state_->header) {
        error = "parser capture client is not connected";
        return Status::InvalidArgument;
    }
    if (state_->stopped) return Status::Ok;
    if (!producerDisconnected()) {
        error = "parser capture producer is still connected";
        return Status::InvalidArgument;
    }
    const std::uint64_t deadline =
        clockNowNs(CLOCK_MONOTONIC) + kHandshakeTimeoutNs;
    for (;;) {
        bool inFlight = false;
        for (const auto& shard : state_->shards) {
            if (shard.header->inFlightPublishes.load(
                    std::memory_order_seq_cst) != 0u) {
                inFlight = true;
                break;
            }
        }
        if (state_->header->consumerEpoch.load(std::memory_order_seq_cst) !=
                state_->consumerEpoch &&
            !inFlight) {
            state_->finalLossEpoch =
                state_->header->lossEpoch.load(std::memory_order_acquire);
            state_->stopped = true;
            return Status::Ok;
        }
        const std::uint64_t now = clockNowNs(CLOCK_MONOTONIC);
        if (now == 0u || now >= deadline) {
            error = "parser capture disconnect did not reach a quiescent boundary";
            return Status::CorruptData;
        }
        std::this_thread::yield();
    }
}

Status ParserMarketCaptureClient::appendFrozenLosses(
    corpus::BinaryMarketCorpusWriter& writer,
    std::string& error) noexcept {
    error.clear();
    if (!state_ || !state_->header || state_->sources.empty()) {
        error = "parser capture directory was not loaded";
        return Status::InvalidArgument;
    }
    if (state_->lossesAppended) return Status::Ok;
    if (!state_->stopped && !producerDisconnected()) {
        error = "parser capture loss ledger is not frozen";
        return Status::InvalidArgument;
    }
    const std::uint64_t globalLossEpoch =
        state_->header->lossEpoch.load(std::memory_order_acquire);
    if (state_->stopped && globalLossEpoch != state_->finalLossEpoch) {
        error = "parser capture loss epoch changed after Stop";
        return Status::CorruptData;
    }
    const std::uint64_t observed = clockNowNs(CLOCK_REALTIME);
    if (observed == 0u ||
        observed > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        error = "failed to timestamp parser capture loss observation";
        return Status::IoError;
    }
    std::uint64_t countedLossEpoch = 0u;
    for (std::size_t sourceIndex = 0u;
         sourceIndex < state_->sources.size(); ++sourceIndex) {
        for (std::size_t channelIndex = 0u;
             channelIndex < parser::kMarketCaptureChannelCount;
             ++channelIndex) {
            const auto& cell = state_->lossLedger[
                sourceIndex * parser::kMarketCaptureChannelCount +
                channelIndex];
            const std::uint64_t gapEpoch =
                cell.gapEpoch.load(std::memory_order_acquire);
            if (gapEpoch == 0u) continue;
            const std::uint64_t dropped =
                cell.droppedRecords.load(std::memory_order_relaxed);
            const std::uint64_t minimumReceive =
                cell.minimumDroppedReceiveRealtimeNs.load(
                    std::memory_order_relaxed);
            const std::uint64_t maximumReceive =
                cell.maximumDroppedReceiveRealtimeNs.load(
                    std::memory_order_relaxed);
            const std::uint64_t generation =
                cell.lastSourceGeneration.load(std::memory_order_relaxed);
            const std::uint32_t shard =
                cell.lastShardIndex.load(std::memory_order_relaxed);
            const std::uint32_t lossFlags =
                cell.flags.load(std::memory_order_relaxed);
            const bool arrivalUnavailable =
                (lossFlags & parser::MarketCaptureLossArrivalUnavailable) !=
                0u;
            const bool receiveRangeKnown = !arrivalUnavailable &&
                minimumReceive != 0u && maximumReceive >= minimumReceive;
            const bool receiveRangeUnavailable = arrivalUnavailable;
            if (dropped == 0u || gapEpoch != dropped ||
                (lossFlags & ~parser::kMarketCaptureKnownLossFlags) != 0u ||
                (!receiveRangeKnown && !receiveRangeUnavailable) ||
                maximumReceive > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) ||
                generation == 0u || shard >= state_->shards.size()) {
                error = "parser capture loss ledger contract mismatch";
                return Status::CorruptData;
            }
            corpus::BinaryMarketGap gap{};
            gap.sourceId = static_cast<std::uint32_t>(sourceIndex + 1u);
            gap.sourceGeneration = generation;
            gap.gapEpoch = gapEpoch;
            gap.droppedRecords = dropped;
            gap.firstDroppedEventSequence =
                cell.firstDroppedEventSequence.load(std::memory_order_relaxed);
            gap.lastDroppedEventSequence =
                cell.lastDroppedEventSequence.load(std::memory_order_relaxed);
            gap.minimumDroppedReceiveNs = arrivalUnavailable
                ? 0
                : static_cast<std::int64_t>(minimumReceive);
            gap.maximumDroppedReceiveNs = arrivalUnavailable
                ? 0
                : static_cast<std::int64_t>(maximumReceive);
            gap.observedReceiveNs = static_cast<std::int64_t>(observed);
            gap.shardIndex = static_cast<std::uint16_t>(shard);
            gap.channel = channelIndex==parser::marketCaptureChannelIndex(parser::MarketCaptureChannel::Membership)
                ? corpus::BinaryMarketChannel::SourceLifecycle : static_cast<corpus::BinaryMarketChannel>(channelIndex + 1u);
            const Status status = writer.appendGap(gap);
            if (!isOk(status)) {
                error = "binary market corpus gap ledger write failed";
                return status;
            }
            if (countedLossEpoch >
                std::numeric_limits<std::uint64_t>::max() - gapEpoch) {
                error = "parser capture global loss epoch overflow";
                return Status::CorruptData;
            }
            countedLossEpoch += gapEpoch;
            ++state_->gapsWritten;
        }
    }
    if (countedLossEpoch != globalLossEpoch) {
        error = "parser capture global loss ledger mismatch";
        return Status::CorruptData;
    }
    state_->lossesAppended = true;
    return Status::Ok;
}

bool ParserMarketCaptureClient::producerDisconnected() const noexcept {
    if (!state_ || !state_->header) return true;
    if (socketPeerDisconnected(state_->socketFd)) return true;
    return !state_->stopped &&
        state_->header->consumerEpoch.load(std::memory_order_acquire) !=
            state_->consumerEpoch;
}

ParserMarketCaptureSnapshot ParserMarketCaptureClient::snapshot() const
    noexcept {
    ParserMarketCaptureSnapshot result{};
    if (!state_ || !state_->header) return result;
    result.connected = true;
    result.stopped = state_->stopped;
    result.producerDisconnected = producerDisconnected();
    result.producerEpoch = state_->header->producerEpoch;
    result.directoryGeneration =
        state_->header->directoryGeneration.load(std::memory_order_acquire);
    result.lossEpoch =
        state_->header->lossEpoch.load(std::memory_order_acquire);
    result.captureStartedReceiveNs = state_->captureStartedReceiveNs;
    result.captureStartedMonotonicNs = state_->captureStartedMonotonicNs;
    result.recordsDrained = state_->recordsDrained;
    result.gapsWritten = state_->gapsWritten;
    result.sourceCount = static_cast<std::uint32_t>(state_->sources.size());
    result.ringCapacity = state_->header->ringCapacity;
    result.shardCount = state_->header->shardCount;
    for (const auto& shard : state_->shards) {
        const std::uint64_t read =
            shard.ring->read.load(std::memory_order_relaxed);
        const std::uint64_t write =
            shard.ring->write.load(std::memory_order_acquire);
        const std::uint64_t pending = write >= read ? write - read : 0u;
        if (result.pendingRecords >
            std::numeric_limits<std::uint64_t>::max() - pending) {
            result.pendingRecords = std::numeric_limits<std::uint64_t>::max();
        } else {
            result.pendingRecords += pending;
        }
    }
    return result;
}

void ParserMarketCaptureClient::disconnect() noexcept {
    state_.reset();
}

}  // namespace hftrec::capture
