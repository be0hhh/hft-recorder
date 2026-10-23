#include "hftrec/Service/RecorderService.hpp"
#include "RecorderServiceProtocol.hpp"
#include <chrono>
#include <fcntl.h>

namespace hftrec::service {
struct RecorderServiceClientState final {int fd{-1};};
namespace {
Status exchange(RecorderServiceClientState* state,detail::Operation operation,const detail::Encoder& input,
    std::vector<std::uint8_t>& response,std::string& error) {
    error.clear();response.clear();
    if(!state || state->fd<0) {error="Recorder service is not connected";return Status::InvalidArgument;}
    const auto packet=detail::packet(operation,input);
    if(packet.empty()) {error="Recorder request exceeds its message bound";return Status::OutOfRange;}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    auto wait=[&](short events) {
        for(;;) {
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(remaining<=0)return false;
            pollfd descriptor{state->fd,events,0};
            const auto result=::poll(&descriptor,1u,static_cast<int>(remaining));
            if(result<0 && errno==EINTR)continue;
            if(result<=0 || (descriptor.revents&POLLNVAL))return false;
            // A peer can close after sending its final packet. Validate those
            // buffered bytes before classifying the following EOF as transport.
            if(events==POLLIN)return (descriptor.revents&POLLIN)!=0;
            return (descriptor.revents&events) && !(descriptor.revents&(POLLERR|POLLHUP));
        }
    };
    bool sent=false;
    while(wait(POLLOUT)) {
        const auto bytes=::send(state->fd,packet.data(),packet.size(),MSG_DONTWAIT|MSG_NOSIGNAL);
        if(bytes==static_cast<ssize_t>(packet.size())) {sent=true;break;}
        if(bytes<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))continue;
        break;
    }
    if(!sent) {::close(state->fd);state->fd=-1;error="Recorder control send failed; outcome unknown, request will not be resent";return Status::IoError;}
    response.resize(kRecorderServiceMaximumMessage);
    for(;;) {
        if(!wait(POLLIN))break;
        const auto bytes=::recv(state->fd,response.data(),response.size(),MSG_DONTWAIT|MSG_TRUNC);
        if(bytes<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))continue;
        if(bytes<=0)break;
        if(static_cast<std::size_t>(bytes)>response.size()) {
            ::close(state->fd);state->fd=-1;response.clear();
            error="Recorder reply exceeds the protocol message bound";return Status::CorruptData;
        }
        response.resize(static_cast<std::size_t>(bytes));std::span<const std::uint8_t> body;
        const auto status=detail::decodeReply(response,operation,body,error);
        if(status==Status::CorruptData) {::close(state->fd);state->fd=-1;response.clear();return status;}
        if(!isOk(status)) {response.clear();return status;}
        response.erase(response.begin(),response.end()-static_cast<std::ptrdiff_t>(body.size()));
        return status;
    }
    ::close(state->fd);state->fd=-1;response.clear();
    error="Recorder control reply unavailable; outcome unknown, request will not be resent";return Status::IoError;
}
}
RecorderServiceClient::RecorderServiceClient() noexcept=default;
RecorderServiceClient::~RecorderServiceClient() noexcept {disconnect();}
void RecorderServiceClient::disconnect() noexcept {if(state_ && state_->fd>=0)::close(state_->fd);state_.reset();}
Status RecorderServiceClient::connect(const std::filesystem::path& runtime,std::string& error) noexcept {
    disconnect();error.clear();
    try {
        struct stat directoryInfo{};
        if(::lstat(runtime.c_str(),&directoryInfo)!=0 && (errno==ENOENT || errno==ENOTDIR)) {
            error="Recorder runtime is unavailable";return Status::IoError;
        }
        if(!detail::privateDirectory(runtime,error))return Status::InvalidArgument;
        sockaddr_un address{};if(!detail::addressFor(runtime,address,error))return Status::InvalidArgument;
        struct stat info{};const auto socketPath=runtime/kRecorderServiceSocket;
        if(::lstat(socketPath.c_str(),&info)!=0) {
            const auto missing=errno==ENOENT || errno==ENOTDIR;
            error=missing?"Recorder socket is unavailable":"Recorder socket access rejected";
            return missing?Status::IoError:Status::InvalidArgument;
        }
        if(!S_ISSOCK(info.st_mode) || info.st_uid!=::geteuid() || (info.st_mode&0077u)) {
            error="Recorder socket ownership, type or permissions rejected";return Status::InvalidArgument;
        }
        auto state=std::make_unique<RecorderServiceClientState>();state->fd=::socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
        if(state->fd<0) {error="cannot create Recorder control socket";return Status::IoError;}
        int buffer=static_cast<int>(kRecorderServiceMaximumMessage*2u);
        (void)::setsockopt(state->fd,SOL_SOCKET,SO_SNDBUF,&buffer,sizeof(buffer));
        (void)::setsockopt(state->fd,SOL_SOCKET,SO_RCVBUF,&buffer,sizeof(buffer));
        if(::connect(state->fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0) {
            const auto denied=errno==EACCES || errno==EPERM;
            ::close(state->fd);error=denied?"Recorder service connection permission rejected":"Recorder service transport is unavailable";
            return denied?Status::InvalidArgument:Status::IoError;
        }
        if(!detail::peerIsOwner(state->fd)) {
            ::close(state->fd);error="Recorder service peer ownership rejected";return Status::InvalidArgument;
        }
        state_=std::move(state);return Status::Ok;
    } catch(...) {error="Recorder client allocation failed";return Status::OutOfRange;}
}
Status RecorderServiceClient::configure(const RecorderServiceConfig& config,std::string& error) noexcept {
    try {detail::Encoder input;detail::encodeConfig(input,config);std::vector<std::uint8_t> response;
        auto result=exchange(state_.get(),detail::Operation::Configure,input,response,error);
        return isOk(result)&&!response.empty()?Status::CorruptData:result;
    } catch(...) {error="Recorder configuration encoding failed";return Status::OutOfRange;}
}
Status RecorderServiceClient::start(std::string& error) noexcept {
    try {std::vector<std::uint8_t> response;const auto result=exchange(state_.get(),detail::Operation::Start,{},response,error);
        return isOk(result)&&!response.empty()?Status::CorruptData:result;
    } catch(...) {error="Recorder start control failed; outcome unknown";return Status::Unknown;}
}
Status RecorderServiceClient::requestStop(std::string& error) noexcept {
    try {std::vector<std::uint8_t> response;const auto result=exchange(state_.get(),detail::Operation::Stop,{},response,error);
        return isOk(result)&&!response.empty()?Status::CorruptData:result;
    } catch(...) {error="Recorder stop control failed; outcome unknown";return Status::Unknown;}
}
Status RecorderServiceClient::status(RecorderServiceStatus& output,std::string& error) noexcept {
    output={};try {std::vector<std::uint8_t> response;const auto result=exchange(state_.get(),detail::Operation::Status,{},response,error);
        if(!isOk(result))return result;detail::Decoder decoder(response);detail::decodeStatus(decoder,output);
        if(!decoder.done()) {error="Recorder status reply rejected";return Status::CorruptData;}return Status::Ok;
    } catch(...) {error="Recorder status decoding failed";return Status::CorruptData;}
}
Status RecorderServiceClient::catalog(std::uint32_t offset,std::uint32_t limit,RecorderCatalog& output,std::string& error) noexcept {
    output={};try {detail::Encoder input;input.pod(offset);input.pod(limit);std::vector<std::uint8_t> response;
        const auto result=exchange(state_.get(),detail::Operation::Catalog,input,response,error);if(!isOk(result))return result;
        detail::Decoder decoder(response);decoder.pod(output.cutGeneration);decoder.pod(output.totalSources);decoder.pod(output.nextOffset);
        std::uint32_t count{};decoder.pod(count);if(!decoder.valid || count>256u || count>limit)return Status::CorruptData;
        output.sources.resize(count);for(std::size_t i=0u;i<output.sources.size();++i) {auto& source=output.sources[i];decoder.pod(source);
            if(!corpus::validBinaryMarketSource(source,static_cast<std::size_t>(offset)+i))decoder.valid=false;}
        if(!decoder.done() || output.nextOffset>output.totalSources) {error="Recorder catalog reply rejected";return Status::CorruptData;}return Status::Ok;
    } catch(...) {error="Recorder catalog decoding failed";return Status::CorruptData;}
}
Status RecorderServiceClient::readBlock(std::uint64_t generation,std::uint32_t index,std::uint32_t offset,
    std::uint32_t maximum,RecorderBlockChunk& output,std::string& error) noexcept {
    output={};try {detail::Encoder input;input.pod(generation);input.pod(index);input.pod(offset);input.pod(maximum);
        std::vector<std::uint8_t> response;const auto result=exchange(state_.get(),detail::Operation::Block,input,response,error);if(!isOk(result))return result;
        detail::Decoder decoder(response);decoder.pod(output.cutGeneration);decoder.pod(output.totalBytes);decoder.pod(output.offset);
        decoder.boolean(output.archiveFallback);decoder.boolean(output.available);decoder.boolean(output.subscriberLag);
        decoder.pod(output.range);std::uint32_t count{};decoder.pod(count);
        if(!decoder.valid || count>kRecorderServiceMaximumBlockChunk || count>maximum || count>response.size()-decoder.position ||
           output.offset!=offset || output.offset>output.totalBytes || count>output.totalBytes-output.offset ||
           (!output.available && count!=0u) || output.subscriberLag==output.available)return Status::CorruptData;
        output.bytes.assign(response.begin()+decoder.position,response.begin()+decoder.position+count);decoder.position+=count;
        if(!decoder.done()) {error="Recorder compressed block reply rejected";return Status::CorruptData;}return Status::Ok;
    } catch(...) {error="Recorder compressed block decoding failed";return Status::CorruptData;}
}
Status RecorderServiceClient::sessions(std::uint32_t offset,std::uint32_t limit,RecorderSessionCatalog& output,std::string& error) noexcept {
    output={};try {
        detail::Encoder input;input.pod(offset);input.pod(limit);std::vector<std::uint8_t> response;
        const auto result=exchange(state_.get(),detail::Operation::Sessions,input,response,error);if(!isOk(result))return result;
        detail::Decoder decoder(response);std::uint32_t count{};decoder.pod(output.totalSessions);decoder.pod(output.nextOffset);decoder.pod(count);
        if(!decoder.valid || count>64u || count>limit || output.nextOffset>output.totalSessions)return Status::CorruptData;
        output.sessions.resize(count);
        for(auto& value:output.sessions) {
            decoder.path(value.sourcePath);decoder.string(value.sourceIdentity);decoder.pod(value.producerEpoch);decoder.pod(value.cutGeneration);
            decoder.pod(value.startedReceiveNs);decoder.pod(value.endedReceiveNs);decoder.pod(value.sourceCount);
            decoder.boolean(value.active);decoder.boolean(value.complete);
        }
        if(!decoder.done()) {error="Recorder session catalog reply rejected";return Status::CorruptData;}
        return Status::Ok;
    } catch(...) {error="Recorder session catalog decoding failed";return Status::CorruptData;}
}
Status RecorderServiceClient::sessionSources(const std::filesystem::path& path,std::uint32_t offset,std::uint32_t limit,
    RecorderCatalog& output,std::string& error) noexcept {
    output={};try {
        detail::Encoder input;input.path(path);input.pod(offset);input.pod(limit);std::vector<std::uint8_t> response;
        const auto result=exchange(state_.get(),detail::Operation::SessionSources,input,response,error);if(!isOk(result))return result;
        detail::Decoder decoder(response);std::uint32_t count{};decoder.pod(output.cutGeneration);decoder.pod(output.totalSources);
        decoder.pod(output.nextOffset);decoder.pod(count);
        if(!decoder.valid || count>256u || count>limit || output.nextOffset>output.totalSources)return Status::CorruptData;
        output.sources.resize(count);
        for(std::size_t i=0u;i<output.sources.size();++i) {decoder.pod(output.sources[i]);
            if(!corpus::validBinaryMarketSource(output.sources[i],static_cast<std::size_t>(offset)+i))decoder.valid=false;}
        if(!decoder.done()) {error="Recorder session sources reply rejected";return Status::CorruptData;}
        return Status::Ok;
    } catch(...) {error="Recorder session source decoding failed";return Status::CorruptData;}
}
} // namespace hftrec::service
