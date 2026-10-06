#include "RecorderCaptureSession.hpp"
#include "Private/ManagedParser.hpp"
#include "../Parser/ParserMarketCaptureClient.hpp"
#include "../Parser/BufferedCorpusWriter.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <spawn.h>
#include <string>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace hftrec::capture {
namespace {
using Clock=std::chrono::steady_clock;
using detail::OwnedChild;
std::int64_t realtimeNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
bool runChild(OwnedChild& child,const std::vector<std::string>& args,int logFd,
              const std::atomic<bool>& stop,std::string& error) {
    if(!child.spawn(args,logFd,error)) return false;
    const auto deadline=Clock::now()+std::chrono::seconds(60);
    while(child.running()) {
        if(stop.load(std::memory_order_acquire) || Clock::now()>=deadline) {
            child.stop();error="Parser launch preparation cancelled or timed out";return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!WIFEXITED(child.exitStatus) || WEXITSTATUS(child.exitStatus)!=0) {
        error="Parser launch preparation failed; inspect the private launch log";return false;
    }
    return true;
}
bool resolveBuild(const std::filesystem::path& root,const std::atomic<bool>& stop,
                  std::filesystem::path& build,std::string& error) {
    int pipeFds[2]{};
    if(::pipe2(pipeFds,O_CLOEXEC|O_NONBLOCK)!=0) {error="cannot open build resolver output";return false;}
    OwnedChild child;
    if(!child.spawn({"python3",(root/"tools/Build/ActiveBuild.py").string(),"--root",root.string(),"--owner","hft-parser"},pipeFds[1],error)) {
        ::close(pipeFds[0]);::close(pipeFds[1]);return false;
    }
    ::close(pipeFds[1]);std::string text;bool okay=true;
    const auto deadline=Clock::now()+std::chrono::seconds(15);
    for(;;) {
        char bytes[512]{};const auto count=::read(pipeFds[0],bytes,sizeof(bytes));
        if(count>0) {
            if(text.size()+static_cast<std::size_t>(count)>4096u) {okay=false;break;}
            text.append(bytes,static_cast<std::size_t>(count));continue;
        }
        if(count==0 && !child.running()) break;
        if(count<0 && errno!=EAGAIN && errno!=EINTR) {okay=false;break;}
        if(stop.load(std::memory_order_acquire) || Clock::now()>=deadline) {okay=false;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ::close(pipeFds[0]);if(!okay) child.stop();
    if(!okay || !WIFEXITED(child.exitStatus) || WEXITSTATUS(child.exitStatus)!=0) {
        error="canonical Parser active-build resolver failed";return false;
    }
    while(!text.empty() && (text.back()=='\n' || text.back()=='\r')) text.pop_back();
    if(text.empty() || text.front()!='/' || text.find('\n')!=std::string::npos) {
        error="canonical Parser build resolver returned an invalid path";return false;
    }
    build=text;return true;
}
bool executable(const std::filesystem::path& path) {
    struct stat info{};
    return ::lstat(path.c_str(),&info)==0 && S_ISREG(info.st_mode) && ::access(path.c_str(),X_OK)==0;
}
bool writePrivateConfig(const std::filesystem::path& directory,const std::string& text,
                        std::filesystem::path& output,std::string& error) {
    auto name=(directory/".recorder-capture-XXXXXX.ini").string();
    std::vector<char> mutableName(name.begin(),name.end());mutableName.push_back('\0');
    const int fd=::mkstemps(mutableName.data(),4);
    if(fd<0) {error="cannot create secret-free capture configuration next to Parser template";return false;}
    output=mutableName.data();bool okay=::fchmod(fd,0600)==0;
    std::size_t position=0u;
    while(okay && position<text.size()) {
        const auto bytes=::write(fd,text.data()+position,text.size()-position);
        if(bytes<0 && errno==EINTR) continue;
        if(bytes<=0) {okay=false;break;}
        position+=static_cast<std::size_t>(bytes);
    }
    if(okay) okay=::fsync(fd)==0;
    if(::close(fd)!=0) okay=false;
    if(!okay) {std::error_code ignored;std::filesystem::remove(output,ignored);output.clear();error="capture configuration write failed";}
    return okay;
}
} // namespace

struct RecorderCaptureSessionState final {
    detail::OwnedParser producer{};
    RecorderCaptureSessionConfig config{};
    std::string renderedConfig{};
    RecorderCaptureSessionConfig requestedConfig{};
    std::string requestedRendered{};
    std::uint64_t requestedRevision{0u};
    std::atomic<bool> stopRequested{false};
    std::thread worker{};
    mutable std::mutex mutex{};
    RecorderCaptureSessionSnapshot snapshot{};
    std::vector<corpus::BinaryMarketSource> sources{};
};

namespace {
void stopOwnedProducer(RecorderCaptureSessionState& state) noexcept {
    if(state.worker.joinable()) state.worker.join();
    state.producer.stop();
    std::lock_guard lock(state.mutex);
    state.snapshot.producerRetained=false;state.snapshot.producerPid=0;
}
void runSession(RecorderCaptureSessionState& state) noexcept {
    auto& owner=state.producer;
    auto& transientConfig=owner.configPath;auto& runtime=owner.runtimePath;
    auto& parser=owner.child;auto& logFd=owner.logFd;
    std::filesystem::path sessionPath;
    auto publishError=[&](Status status,const std::string& error) {
        std::lock_guard lock(state.mutex);state.snapshot.status=status;state.snapshot.error=error;
    };
    bool successful=false;
    try {
        auto base=std::filesystem::path{std::getenv("XDG_RUNTIME_DIR")?std::getenv("XDG_RUNTIME_DIR"):"/tmp"};
        auto pattern=(base/("recorder-parser-"+std::to_string(::getuid())+"-XXXXXX")).string();
        std::vector<char> directory(pattern.begin(),pattern.end());directory.push_back('\0');
        if(!::mkdtemp(directory.data())) {publishError(Status::IoError,"cannot create private Parser runtime");throw Status::IoError;}
        runtime=directory.data();
        std::filesystem::create_directory(runtime/"state");
        std::filesystem::permissions(runtime/"state",std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
        {
            std::lock_guard lock(state.mutex);state.snapshot.runtimePath=runtime;
            sessionPath=state.config.outputRoot/("capture-"+runtime.filename().string());
            state.snapshot.sessionPath=sessionPath;
        }
        std::string error;
        logFd=::open((runtime/"launch.log").c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
        if(logFd<0 || !writePrivateConfig(state.config.parserTemplate.parent_path(),state.renderedConfig,transientConfig,error)) {
            publishError(Status::IoError,error.empty()?"cannot open private Parser launch log":error);throw Status::IoError;
        }
        std::filesystem::path build;
        if(!resolveBuild(state.config.workspaceRoot,state.stopRequested,build,error)) {publishError(Status::IoError,error);throw Status::IoError;}
        const auto binaryRoot=build/"apps/hft-parser";
        const auto parserBinary=binaryRoot/"hft-parserd",plannerBinary=binaryRoot/"hft-resource-planner";
        if(!executable(parserBinary) || !executable(plannerBinary)) {publishError(Status::IoError,"selected Parser product binaries are missing; build the Parser product first");throw Status::IoError;}
        OwnedChild planner;
        if(!runChild(planner,{plannerBinary.string(),"--config",transientConfig.string(),"--runtime-dir",runtime.string()},logFd,state.stopRequested,error)) {
            publishError(Status::IoError,error);throw Status::IoError;
        }
        const auto topology=runtime/"resource-topology.v3";
        if(!std::filesystem::is_regular_file(topology)) {publishError(Status::IoError,"Parser planner did not produce resource topology");throw Status::IoError;}
        if(!parser.spawn({parserBinary.string(),transientConfig.string(),"--runtime-dir",runtime.string(),"--state-dir",(runtime/"state").string(),"--resource-topology",topology.string()},logFd,error)) {
            publishError(Status::IoError,error);throw Status::IoError;
        }
        {std::lock_guard lock(state.mutex);state.snapshot.producerPid=parser.pid;}
        ParserMarketCaptureClient client;Status status=Status::IoError;
        const auto connectDeadline=Clock::now()+std::chrono::seconds(30);
        while(!state.stopRequested.load(std::memory_order_acquire) && parser.running() && Clock::now()<connectDeadline) {
            status=client.connect(runtime,error);
            if(isOk(status)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if(!isOk(status)) {publishError(status,error.empty()?"owned Parser did not publish its capture plane":error);throw status;}
        std::vector<corpus::BinaryMarketSource> sources;
        status=client.loadSourceDirectory(sources,error);
        if(!isOk(status)) {publishError(status,error);throw status;}
        const auto attached=client.snapshot();
        corpus::BinaryMarketCorpusWriter writer;
        corpus::BinaryMarketWriterConfig config{};
        config.root=sessionPath;config.sources=sources;config.producerEpoch=attached.producerEpoch;
        config.maximumBytes=state.config.maximumBytes;config.segmentTargetBytes=state.config.segmentBytes;
        config.targetDurationNs=state.config.durationSec*1'000'000'000u;
        config.startedReceiveNs=attached.captureStartedReceiveNs;config.startedMonotonicNs=attached.captureStartedMonotonicNs;
        config.ringCapacity=attached.ringCapacity;config.shardCount=attached.shardCount;config.pendingRecordCapacity=8192u;
        if(config.targetDurationNs>std::numeric_limits<std::uint64_t>::max()-config.startedMonotonicNs) {
            (void)client.stop(error);publishError(Status::OutOfRange,"capture duration exceeds monotonic clock range");throw Status::OutOfRange;
        }
        status=writer.start(config);
        if(!isOk(status)) {(void)client.stop(error);publishError(status,"compressed corpus cannot start: output or final drain quota is invalid");throw status;}
        BufferedCorpusWriter buffered(writer);
        status=buffered.start(8192u);
        if(!isOk(status)) {(void)client.stop(error);(void)writer.finalize(corpus::BinaryMarketStopReason::Error,realtimeNs());publishError(status,"bounded capture writer queue failed");throw status;}
        {
            std::lock_guard lock(state.mutex);state.sources=sources;state.snapshot.connected=true;
            state.snapshot.sourceCount=static_cast<std::uint32_t>(sources.size());state.snapshot.producerEpoch=attached.producerEpoch;
        }
        std::array<std::uint64_t,corpus::kBinaryMarketChannelCount> counts{};
        struct Sink {BufferedCorpusWriter* writer;decltype(counts)* counters;RecorderCaptureSessionState* state;};
        Sink sink{&buffered,&counts,&state};
        const auto callback=[](void* context,const corpus::BinaryMarketRecord& record) noexcept {
            auto& target=*static_cast<Sink*>(context);const auto result=target.writer->append(record);
            if(isOk(result) && corpus::validBinaryMarketChannel(record.header.channel))
                ++(*target.counters)[corpus::binaryMarketChannelIndex(record.header.channel)];
            if(isOk(result) && record.header.channel==corpus::BinaryMarketChannel::SourceLifecycle) {
                const auto& source=corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&record)->source;
                std::lock_guard lock(target.state->mutex);
                if(source.sourceId<=target.state->sources.size()) {
                    target.state->sources[source.sourceId-1u]=source;
                    ++target.state->snapshot.sourceMetadataRevision;
                }
            }
            return result;
        };
        const auto sourceCallback=[](void* context,const corpus::BinaryMarketSource& source) noexcept {
            auto& target=*static_cast<Sink*>(context);const auto result=target.writer->appendSource(source);
            if(isOk(result)) {
                try {
                    std::lock_guard lock(target.state->mutex);
                    if(source.sourceId==target.state->sources.size()+1u) {
                        target.state->sources.push_back(source);++target.state->snapshot.sourceMetadataRevision;
                    }
                    target.state->snapshot.sourceCount=static_cast<std::uint32_t>(target.state->sources.size());
                } catch(...) {return Status::IoError;}
            }
            return result;
        };
        std::vector<RecorderSubscriptionChange> changes;
        std::size_t changePosition=0u;std::uint64_t handledRevision=0u,workingRevision=0u;
        RecorderCaptureSessionConfig workingSelection;
        bool controlPending=false;
        auto selectionError=[&](Status outcome,const std::string& message) {
            std::lock_guard lock(state.mutex);state.snapshot.selectionPending=false;
            state.snapshot.error=message.empty()?"Parser refused the requested subscription change":message;
            state.snapshot.status=outcome;
        };
        auto selectionApplied=[&] {
            std::lock_guard lock(state.mutex);state.snapshot.appliedSelectionRevision=workingRevision;
            state.snapshot.channelMask=workingSelection.channelMask;
            state.snapshot.selectionPending=state.requestedRevision>workingRevision;
            state.snapshot.error.clear();state.snapshot.status=Status::Ok;
        };
        auto reason=corpus::BinaryMarketStopReason::Duration;bool failed=false,frozen=false;
        const auto deadline=config.startedMonotonicNs+config.targetDurationNs;
        auto nextSnapshot=Clock::now();
        while(!state.stopRequested.load(std::memory_order_acquire) &&
              static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count())<deadline) {
            if(client.producerDisconnected() || !parser.running()) {
                reason=corpus::BinaryMarketStopReason::ParserDisconnected;
                status=client.freezeDisconnected(error);frozen=isOk(status);failed=!frozen;break;
            }
            if(controlPending) {
                bool complete=false;Status outcome=Status::Unknown;
                status=client.pollSubscriptionChange(complete,outcome,error);
                if(!isOk(status)) {failed=true;reason=corpus::BinaryMarketStopReason::Error;break;}
                if(complete) {
                    controlPending=false;
                    if(isOk(outcome)) ++changePosition;
                    else {selectionError(outcome,error);changes.clear();changePosition=0u;handledRevision=workingRevision;}
                }
            }
            if(!controlPending) {
                std::uint64_t requested=0u;
                {
                    std::lock_guard lock(state.mutex);requested=state.requestedRevision;
                    if(requested>handledRevision && requested!=workingRevision) {
                        workingSelection=state.requestedConfig;workingRevision=requested;
                        changes.clear();changePosition=0u;
                    }
                }
                if(requested>handledRevision && changes.empty()) {
                    std::vector<corpus::BinaryMarketSource> directory;
                    status=client.loadSourceDirectory(directory,error);
                    if(isOk(status)) status=planRecorderSubscriptionChanges(workingSelection,directory,changes,error);
                    if(!isOk(status)) {selectionError(status,error);handledRevision=workingRevision;}
                    else if(changes.empty()) {selectionApplied();handledRevision=workingRevision;}
                }
                if(changePosition<changes.size()) {
                    const auto& change=changes[changePosition];
                    status=client.beginSubscriptionChange(change.sourceId,change.channel,change.add,error);
                    if(isOk(status)) controlPending=true;
                    else {selectionError(status,error);changes.clear();changePosition=0u;handledRevision=workingRevision;}
                } else if(!changes.empty()) {
                    selectionApplied();handledRevision=workingRevision;changes.clear();changePosition=0u;
                }
            }
            std::uint64_t drained=0u;
            status=client.drainTo(callback,&sink,8192u,drained,error,sourceCallback);
            if(status==Status::OutOfRange) {reason=corpus::BinaryMarketStopReason::Quota;break;}
            if(!isOk(status)) {failed=true;reason=corpus::BinaryMarketStopReason::Error;break;}
            if(Clock::now()>=nextSnapshot) {
                std::lock_guard lock(state.mutex);state.snapshot.channelRecords=counts;state.snapshot.writer=buffered.snapshot();
                nextSnapshot=Clock::now()+std::chrono::milliseconds(250);
            }
            if(drained==0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(state.stopRequested.load(std::memory_order_acquire) && reason!=corpus::BinaryMarketStopReason::ParserDisconnected)
            reason=corpus::BinaryMarketStopReason::Requested;
        if(controlPending) {
            const auto controlDeadline=Clock::now()+std::chrono::milliseconds(2100);
            bool ingestAllowed=status!=Status::OutOfRange && buffered.status()==Status::Ok;
            while(controlPending && Clock::now()<controlDeadline) {
                bool complete=false;Status outcome=Status::Unknown;
                status=client.pollSubscriptionChange(complete,outcome,error);
                if(!isOk(status)) {failed=true;break;}
                if(complete) {controlPending=false;if(!isOk(outcome)) selectionError(outcome,error);}
                if(controlPending && ingestAllowed) {
                    std::uint64_t drained=0u;
                    const auto ingestStatus=client.drainTo(callback,&sink,8192u,drained,error,sourceCallback);
                    if(ingestStatus==Status::OutOfRange) ingestAllowed=false;
                    else if(!isOk(ingestStatus)) {failed=true;break;}
                }
                if(controlPending) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(controlPending) {failed=true;error="subscription outcome unknown at capture stop; command will not be resent";}
        }
        if(!frozen) {
            status=client.stop(error);
            if(isOk(status)) frozen=true;
            else if(client.producerDisconnected()) {status=client.freezeDisconnected(error);frozen=isOk(status);}
            if(!frozen) failed=true;
        }
        status=buffered.finish();
        if(status!=Status::Ok && status!=Status::OutOfRange) failed=true;
        if(frozen && isOk(writer.beginFinalDrain())) {
            if(!isOk(buffered.drainFrozen())) failed=true;
            struct FinalSink {corpus::BinaryMarketCorpusWriter* writer;decltype(counts)* counters;RecorderCaptureSessionState* state;};
            FinalSink finalSink{&writer,&counts,&state};
            while(!failed && client.snapshot().pendingRecords!=0u) {
                std::uint64_t drained=0u;
                status=client.drainTo([](void* context,const corpus::BinaryMarketRecord& record) noexcept {
                    auto& target=*static_cast<FinalSink*>(context);
                    const auto result=target.writer->append(record);
                    if(isOk(result) && corpus::validBinaryMarketChannel(record.header.channel))
                        ++(*target.counters)[corpus::binaryMarketChannelIndex(record.header.channel)];
                    if(isOk(result) && record.header.channel==corpus::BinaryMarketChannel::SourceLifecycle) {
                        const auto& source=corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&record)->source;
                        std::lock_guard lock(target.state->mutex);
                        if(source.sourceId<=target.state->sources.size()) {
                            target.state->sources[source.sourceId-1u]=source;++target.state->snapshot.sourceMetadataRevision;
                        }
                    }
                    return result;
                },&finalSink,0u,drained,error,[](void* context,const corpus::BinaryMarketSource& source) noexcept {
                    auto& target=*static_cast<FinalSink*>(context);
                    const auto result=target.writer->registerSources(std::span<const corpus::BinaryMarketSource>{&source,1u});
                    if(isOk(result)) {
                        try {
                            std::lock_guard lock(target.state->mutex);
                            if(source.sourceId==target.state->sources.size()+1u) {
                                target.state->sources.push_back(source);++target.state->snapshot.sourceMetadataRevision;
                            }
                            target.state->snapshot.sourceCount=static_cast<std::uint32_t>(target.state->sources.size());
                        } catch(...) {return Status::IoError;}
                    }
                    return result;
                });
                if(!isOk(status) || drained==0u) failed=true;
            }
            if(!isOk(client.appendFrozenLosses(writer,error))) failed=true;
        } else failed=true;
        if(client.snapshot().lossEpoch!=0u) {
            failed=true;
            if(error.empty()) error="Parser capture lost records; corpus incomplete";
        }
        if(failed) reason=corpus::BinaryMarketStopReason::Error;
        status=writer.finalize(reason,realtimeNs());
        {
            std::lock_guard lock(state.mutex);state.snapshot.channelRecords=counts;state.snapshot.writer=writer.snapshot();
            state.snapshot.complete=!failed && isOk(status);state.snapshot.status=state.snapshot.complete?Status::Ok:Status::IoError;
            if(!state.snapshot.complete) state.snapshot.error=error.empty()?"capture failed; corpus remains incomplete":error;
        }
        successful=!failed && isOk(status);
    } catch(...) {
        std::lock_guard lock(state.mutex);
        if(state.snapshot.error.empty()) state.snapshot.error="Recorder-owned Parser capture failed";
        if(state.snapshot.status==Status::Ok) state.snapshot.status=Status::IoError;
    }
    {
        std::lock_guard lock(state.mutex);state.snapshot.active=false;state.snapshot.connected=false;
        // Serialize the final stop decision with requestStop's active snapshot:
        // a late explicit request either stops here or joins and stops afterwards.
        const bool explicitStop=state.stopRequested.load(std::memory_order_acquire);
        owner.finishCapture(explicitStop);
        state.snapshot.producerRetained=owner.retained;
        state.snapshot.producerPid=parser.pid>0?parser.pid:0;
        if(!successful && !explicitStop && !runtime.empty()) state.snapshot.error+=" (launch log: "+(runtime/"launch.log").string()+")";
    }
}
} // namespace

std::filesystem::path findRecorderWorkspaceRoot() noexcept {
    try {
        std::error_code error;auto path=std::filesystem::read_symlink("/proc/self/exe",error);
        for(auto candidate:{error?std::filesystem::current_path():path.parent_path(),std::filesystem::current_path()}) {
            for(unsigned n=0u;n<12u && !candidate.empty();++n) {
                if(std::filesystem::is_regular_file(candidate/"tools/Build/ActiveBuild.py")) return candidate;
                const auto parent=candidate.parent_path();if(parent==candidate) break;candidate=parent;
            }
        }
    } catch(...) {}
    return {};
}
RecorderCaptureSession::RecorderCaptureSession() noexcept=default;
RecorderCaptureSession::~RecorderCaptureSession() noexcept {(void)stop();}
Status RecorderCaptureSession::start(const RecorderCaptureSessionConfig& config,std::string& error) noexcept {
    error.clear();
    try {
        if(state_ && snapshot().active) {error="one shared capture session is already active";return Status::InvalidArgument;}
        if(state_ && state_->worker.joinable()) state_->worker.join();
        if(state_ && state_->producer.child.running()) {
            error="Recorder-owned Parser is retained after capture; stop the current session before starting another capture";
            return Status::InvalidArgument;
        }
        if(config.workspaceRoot.empty() || !std::filesystem::is_regular_file(config.workspaceRoot/"tools/Build/ActiveBuild.py") ||
           config.parserTemplate.empty() || config.outputRoot.empty() || !config.parserTemplate.is_absolute()) {
            error="capture needs an existing CXET workspace and absolute Parser template";return Status::InvalidArgument;
        }
        const auto expected=std::filesystem::weakly_canonical(config.parserTemplate.parent_path()/".env");
        if(config.envPath.empty() || std::filesystem::weakly_canonical(config.envPath)!=expected) {
            error="Parser credentials must use the existing .env beside its selected template; choose that path";return Status::InvalidArgument;
        }
        std::ifstream file(config.parserTemplate,std::ios::binary);
        if(!file.is_open() || std::filesystem::file_size(config.parserTemplate)>1024u*1024u) {error="Parser template unavailable or oversized";return Status::IoError;}
        const std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        auto state=std::make_unique<RecorderCaptureSessionState>();
        auto status=renderRecorderParserConfig(text,config,state->renderedConfig,error);
        if(!isOk(status)) return status;
        state->config=config;state->requestedConfig=config;state->requestedRendered=state->renderedConfig;state->snapshot.active=true;state->snapshot.channelMask=config.channelMask;
        state_=std::move(state);state_->worker=std::thread(runSession,std::ref(*state_));return Status::Ok;
    } catch(...) {
        if(state_ && !state_->worker.joinable()) {
            std::lock_guard lock(state_->mutex);state_->snapshot.active=false;state_->snapshot.status=Status::IoError;
        }
        error="cannot prepare Recorder-owned Parser capture";return Status::IoError;
    }
}
Status RecorderCaptureSession::updateSelection(const RecorderCaptureSessionConfig& config,std::string& error) noexcept {
    error.clear();
    if(!state_ || !snapshot().active) {error="capture is not active";return Status::InvalidArgument;}
    try {
        if(config.maximumBytes!=state_->config.maximumBytes || config.durationSec!=state_->config.durationSec ||
           config.outputRoot!=state_->config.outputRoot || config.envPath!=state_->config.envPath ||
           config.parserTemplate!=state_->config.parserTemplate || config.segmentBytes!=state_->config.segmentBytes ||
           config.workspaceRoot!=state_->config.workspaceRoot) {
            error="storage, duration, credentials and Parser template require a new capture session";return Status::InvalidArgument;
        }
        std::ifstream file(config.parserTemplate,std::ios::binary);
        if(!file.is_open() || std::filesystem::file_size(config.parserTemplate)>1024u*1024u) return Status::IoError;
        const std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};std::string rendered;
        const auto status=renderRecorderParserConfig(text,config,rendered,error);if(!isOk(status)) return status;
        std::lock_guard lock(state_->mutex);
        if(rendered==state_->requestedRendered) {
            if(!state_->snapshot.selectionPending && !isOk(state_->snapshot.status)) {
                error=state_->snapshot.error;return state_->snapshot.status;
            }
            return Status::Ok;
        }
        if(state_->stopRequested.load(std::memory_order_acquire)) {error="capture is already stopping";return Status::Cancelled;}
        if(state_->requestedRevision==std::numeric_limits<std::uint64_t>::max()) return Status::OutOfRange;
        state_->requestedConfig=config;state_->requestedRendered=std::move(rendered);++state_->requestedRevision;
        state_->snapshot.selectionPending=true;state_->snapshot.error.clear();
        return Status::Ok;
    } catch(...) {error="capture selection mailbox allocation failed";return Status::IoError;}
}
void RecorderCaptureSession::requestStop() noexcept {
    if(!state_) return;
    state_->stopRequested.store(true,std::memory_order_release);
    if(!snapshot().active) stopOwnedProducer(*state_);
}
Status RecorderCaptureSession::stop() noexcept {
    requestStop();if(state_) stopOwnedProducer(*state_);
    return state_?snapshot().status:Status::Ok;
}
RecorderCaptureSessionSnapshot RecorderCaptureSession::snapshot() const noexcept {
    if(!state_) return {};
    try {std::lock_guard lock(state_->mutex);return state_->snapshot;} catch(...) {RecorderCaptureSessionSnapshot s;s.status=Status::IoError;return s;}
}
std::vector<corpus::BinaryMarketSource> RecorderCaptureSession::sourceDirectory() const noexcept {
    if(!state_) return {};
    try {std::lock_guard lock(state_->mutex);return state_->sources;} catch(...) {return {};}
}
} // namespace hftrec::capture
