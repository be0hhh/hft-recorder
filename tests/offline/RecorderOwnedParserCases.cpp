#include "OfflineCase.hpp"
#include "Capture/Coordinator/Private/ManagedParser.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using hftrec::capture::detail::OwnedParser;
std::filesystem::path prepare(OwnedParser& parser) {
    char pattern[]="/tmp/hftrec-owned-parser-XXXXXX";
    const auto* path=::mkdtemp(pattern);CXET_CHECK(path);parser.runtimePath=path;
    parser.configPath=parser.runtimePath/"capture.ini";
    {std::ofstream file(parser.configPath);file<<"[runtime]\n";CXET_CHECK(file.good());}
    parser.logFd=::open((parser.runtimePath/"launch.log").c_str(),O_WRONLY|O_CREAT|O_CLOEXEC,0600);
    CXET_CHECK(parser.logFd>=0);std::string error;
    CXET_CHECK(parser.child.spawn({"/proc/self/exe","--owned-dummy-child"},parser.logFd,error));
    CXET_CHECK(parser.child.running());return parser.runtimePath;
}
bool reaped(pid_t child) {
    int status=0;errno=0;
    return ::waitpid(child,&status,WNOHANG)<0 && errno==ECHILD;
}
void automaticCaptureFailureKeepsOwnedParser() {
    // Error and quota callers both finish capture without an explicit user Stop.
    OwnedParser parser;const auto path=prepare(parser);const auto pid=parser.child.pid;
    parser.finishCapture(false);
    CXET_CHECK(parser.child.running() && parser.child.pid==pid);
    CXET_CHECK(parser.retained && std::filesystem::exists(parser.configPath));
    CXET_CHECK(std::filesystem::exists(path/"launch.log") && parser.logFd>=0);
    parser.stop();CXET_CHECK(reaped(pid) && !std::filesystem::exists(path));
}
void explicitStopCleansOnlyOwnedParser() {
    hftrec::capture::detail::OwnedChild foreign;std::string error;
    const auto nullFd=::open("/dev/null",O_WRONLY|O_CLOEXEC);CXET_CHECK(nullFd>=0);
    CXET_CHECK(foreign.spawn({"/proc/self/exe","--owned-dummy-child"},nullFd,error));::close(nullFd);
    OwnedParser parser;const auto path=prepare(parser);const auto pid=parser.child.pid;
    parser.finishCapture(false);parser.stop();
    CXET_CHECK(reaped(pid) && !parser.retained && parser.logFd<0);
    CXET_CHECK(!std::filesystem::exists(path) && foreign.running());
    foreign.stop();
}
void sessionOwnershipDestructorCleansRetainedParser() {
    std::filesystem::path path;pid_t pid=-1;
    {OwnedParser parser;path=prepare(parser);pid=parser.child.pid;parser.finishCapture(false);}
    CXET_CHECK(reaped(pid) && !std::filesystem::exists(path));
}
} // namespace
int main(int argc,char** argv) {
    if(argc==2 && std::string_view{argv[1]}=="--owned-dummy-child") {
        for(;;) ::pause();
    }
    const cxet::testing::Case cases[]{
        cxet::testing::Case{"capture.automatic_failure_or_quota_retains_owned_parser",automaticCaptureFailureKeepsOwnedParser},
        cxet::testing::Case{"capture.explicit_stop_cleans_only_owned_parser",explicitStopCleansOnlyOwnedParser},
        cxet::testing::Case{"capture.session_owner_destructor_cleans_retained_parser",sessionOwnershipDestructorCleansRetainedParser}};
    return cxet::testing::runCases(argc,argv,cases);
}
