#pragma once
#include "hftrec/Service/RecorderService.hpp"
#include "cxet/Primitives/Buf/AsmPrimitives.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <poll.h>
#include <span>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <type_traits>
#include <unistd.h>

namespace hftrec::service::detail {
inline constexpr std::uint32_t kMagic = 0x53524348u;
enum class Operation : std::uint16_t { Configure=1u, Start, Stop, Status, Catalog, Block, Sessions, SessionSources };
struct Header final {
    std::uint32_t magic{kMagic}, size{};
    Operation operation{};
    std::uint32_t reserved{};
};
static_assert(sizeof(Header)==16u);
class Encoder final {
 public:
    std::vector<std::uint8_t> bytes{};
    bool valid{true};
    template<class T> void pod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if(sizeof(T)>kRecorderServiceMaximumMessage-bytes.size()) {valid=false;return;}
        const auto* begin=reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(),begin,begin+sizeof(T));
    }
    void string(const std::string& value,std::size_t maximum=4096u) {
        if(value.size()>maximum || value.find('\0')!=std::string::npos ||
           value.size()+sizeof(std::uint32_t)>kRecorderServiceMaximumMessage-bytes.size()) {valid=false;return;}
        pod(static_cast<std::uint32_t>(value.size()));bytes.insert(bytes.end(),value.begin(),value.end());
    }
    void path(const std::filesystem::path& value) {string(value.string());}
    void boolean(bool value) {pod(static_cast<std::uint8_t>(value));}
};
class Decoder final {
 public:
    explicit Decoder(std::span<const std::uint8_t> value): bytes(value) {}
    std::span<const std::uint8_t> bytes;
    std::size_t position{};
    bool valid{true};
    template<class T> void pod(T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if(!valid || sizeof(T)>bytes.size()-position) {valid=false;return;}
        cxet::bytescan::hftMemcpy(&value,bytes.data()+position,sizeof(T));position+=sizeof(T);
    }
    void string(std::string& value,std::size_t maximum=4096u) {
        std::uint32_t size{};pod(size);
        if(!valid || size>maximum || size>bytes.size()-position) {valid=false;return;}
        value.assign(reinterpret_cast<const char*>(bytes.data()+position),size);position+=size;
        if(value.find('\0')!=std::string::npos) valid=false;
    }
    void path(std::filesystem::path& value) {std::string text;string(text);if(valid)value=text;}
    void boolean(bool& value) {std::uint8_t flag{};pod(flag);if(flag>1u)valid=false;value=flag!=0u;}
    [[nodiscard]] bool done() const {return valid && position==bytes.size();}
};
inline void encodeConfig(Encoder& out,const RecorderServiceConfig& config) {
    out.path(config.workspaceRoot);out.path(config.parserTemplate);out.path(config.envPath);out.path(config.outputRoot);
    out.pod(config.channelMask);out.pod(config.durationSec);out.pod(config.maximumBytes);out.pod(config.segmentBytes);
    out.pod(config.retainedCompressedBytes);out.pod(config.maximumReferenceBytes);out.pod(config.writerQueueRecords);
    if(config.venues.empty() || config.venues.size()>64u) {out.valid=false;return;}
    out.pod(static_cast<std::uint32_t>(config.venues.size()));
    for(const auto& venue:config.venues) {
        out.string(venue.exchange,48u);out.string(venue.market,48u);out.boolean(venue.fullUniverse);
        if(venue.instruments.size()>4096u) {out.valid=false;return;}
        out.pod(static_cast<std::uint32_t>(venue.instruments.size()));
        for(const auto& symbol:venue.instruments) out.string(symbol,128u);
    }
}
inline void decodeConfig(Decoder& in,RecorderServiceConfig& config) {
    in.path(config.workspaceRoot);in.path(config.parserTemplate);in.path(config.envPath);in.path(config.outputRoot);
    in.pod(config.channelMask);in.pod(config.durationSec);in.pod(config.maximumBytes);in.pod(config.segmentBytes);
    in.pod(config.retainedCompressedBytes);in.pod(config.maximumReferenceBytes);in.pod(config.writerQueueRecords);
    std::uint32_t count{};in.pod(count);if(!in.valid || count==0u || count>64u) {in.valid=false;return;}
    config.venues.resize(count);
    for(auto& venue:config.venues) {
        in.string(venue.exchange,48u);in.string(venue.market,48u);in.boolean(venue.fullUniverse);
        in.pod(count);if(!in.valid || count>4096u) {in.valid=false;return;}
        venue.instruments.resize(count);for(auto& symbol:venue.instruments) in.string(symbol,128u);
    }
}
inline void encodeStatus(Encoder& out,const RecorderServiceStatus& value) {
    for(bool flag:{value.configured,value.active,value.connected,value.complete,value.stopping,value.selectionPending})out.boolean(flag);
    out.pod(value.captureStatus);out.pod(value.producerEpoch);out.pod(value.lossEpoch);out.pod(value.cutGeneration);
    out.pod(value.safeMonotonicNs);out.pod(value.records);out.pod(value.gaps);out.pod(value.projectedBytes);
    out.pod(value.sourceCount);out.pod(value.startedReceiveNs);out.pod(value.startedMonotonicNs);out.path(value.sessionPath);out.string(value.sourceIdentity);out.string(value.error);
}
inline void decodeStatus(Decoder& in,RecorderServiceStatus& value) {
    in.boolean(value.configured);in.boolean(value.active);in.boolean(value.connected);in.boolean(value.complete);
    in.boolean(value.stopping);in.boolean(value.selectionPending);in.pod(value.captureStatus);in.pod(value.producerEpoch);
    in.pod(value.lossEpoch);in.pod(value.cutGeneration);in.pod(value.safeMonotonicNs);in.pod(value.records);in.pod(value.gaps);
    in.pod(value.projectedBytes);in.pod(value.sourceCount);in.pod(value.startedReceiveNs);in.pod(value.startedMonotonicNs);in.path(value.sessionPath);in.string(value.sourceIdentity);in.string(value.error);
    if(value.captureStatus<Status::Ok || value.captureStatus>Status::Unknown) in.valid=false;
}
inline bool privateDirectory(const std::filesystem::path& path,std::string& error) {
    struct stat info{};std::error_code ec;
    if(!path.is_absolute() || path.lexically_normal()!=std::filesystem::canonical(path,ec) || ec ||
       ::lstat(path.c_str(),&info)!=0 || !S_ISDIR(info.st_mode) || info.st_uid!=::geteuid() ||
       (info.st_mode&0077u)!=0u) {error="Recorder runtime must be an absolute, same-UID private directory without symlinks";return false;}
    return true;
}
inline bool peerIsOwner(int fd) {
    ucred peer{};socklen_t size=sizeof(peer);
    return ::getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)==0 && size==sizeof(peer) && peer.uid==::geteuid();
}
inline bool addressFor(const std::filesystem::path& runtime,sockaddr_un& address,std::string& error) {
    address={};address.sun_family=AF_UNIX;const auto path=(runtime/kRecorderServiceSocket).string();
    if(path.size()>=sizeof(address.sun_path)) {error="Recorder runtime socket path is too long";return false;}
    cxet::bytescan::hftMemcpy(address.sun_path,path.c_str(),path.size()+1u);return true;
}
inline std::vector<std::uint8_t> packet(Operation operation,const Encoder& payload) {
    if(!payload.valid || payload.bytes.size()>kRecorderServiceMaximumMessage-sizeof(Header))return {};
    Header header{};header.operation=operation;header.size=static_cast<std::uint32_t>(payload.bytes.size());
    std::vector<std::uint8_t> output(sizeof(header)+payload.bytes.size());
    cxet::bytescan::hftMemcpy(output.data(),&header,sizeof(header));std::copy(payload.bytes.begin(),payload.bytes.end(),output.begin()+sizeof(header));
    return output;
}
inline bool parsePacket(std::span<const std::uint8_t> bytes,Header& header,std::span<const std::uint8_t>& payload) {
    if(bytes.size()<sizeof(header))return false;cxet::bytescan::hftMemcpy(&header,bytes.data(),sizeof(header));
    if(header.magic!=kMagic || header.reserved ||
       header.size!=bytes.size()-sizeof(header) || header.operation<Operation::Configure || header.operation>Operation::SessionSources)return false;
    payload=bytes.subspan(sizeof(header));return true;
}
inline Status decodeReply(std::span<const std::uint8_t> packet,Operation expected,
    std::span<const std::uint8_t>& body,std::string& error) {
    body={};Header header{};std::span<const std::uint8_t> payload;
    if(!parsePacket(packet,header,payload) || header.operation!=expected) {
        error="Recorder reply framing or operation rejected";return Status::CorruptData;
    }
    Decoder decoder(payload);Status status{};decoder.pod(status);decoder.string(error);
    if(!decoder.valid || status<Status::Ok || status>Status::Unknown || (!isOk(status) && !decoder.done())) {
        error="Recorder reply status envelope rejected";return Status::CorruptData;
    }
    if(isOk(status))body=payload.subspan(decoder.position);
    return status;
}
} // namespace hftrec::service::detail
