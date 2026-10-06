#pragma once
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <spawn.h>
#include <signal.h>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;
namespace hftrec::capture::detail {
struct OwnedChild final {
    pid_t pid{-1};int exitStatus{0};
    OwnedChild()=default;
    OwnedChild(const OwnedChild&)=delete;
    OwnedChild& operator=(const OwnedChild&)=delete;
    ~OwnedChild() {stop();}
    bool running() noexcept {
        if(pid<=0) return false;
        const auto result=::waitpid(pid,&exitStatus,WNOHANG);
        if(result==pid || (result<0 && errno==ECHILD)) {pid=-1;return false;}
        return true;
    }
    void stop() noexcept {
        if(!running()) return;
        (void)::kill(pid,SIGTERM);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(running() && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(pid>0) {
            (void)::kill(pid,SIGKILL);
            while(::waitpid(pid,&exitStatus,0)<0 && errno==EINTR) {}
            pid=-1;
        }
    }
    bool spawn(const std::vector<std::string>& arguments,int outputFd,std::string& error) {
        if(arguments.empty() || pid>0) return false;
        std::vector<char*> argv;argv.reserve(arguments.size()+1u);
        for(const auto& arg:arguments) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        if(::posix_spawn_file_actions_init(&actions)!=0) {error="cannot prepare Parser process";return false;}
        (void)::posix_spawn_file_actions_adddup2(&actions,outputFd,STDOUT_FILENO);
        (void)::posix_spawn_file_actions_adddup2(&actions,outputFd,STDERR_FILENO);
        const auto status=::posix_spawnp(&pid,argv[0],&actions,nullptr,argv.data(),environ);
        (void)::posix_spawn_file_actions_destroy(&actions);
        if(status!=0) {pid=-1;error="cannot launch selected Parser executable";return false;}
        return true;
    }
};
// One session owns its Parser and every private launch resource through capture
// completion. These are never detached, adopted, or kept in a global registry.
struct OwnedParser final {
    OwnedChild child{};
    std::filesystem::path configPath{},runtimePath{};
    int logFd{-1};
    bool retained{false};
    ~OwnedParser() {stop();}
    void finishCapture(bool explicitStop) noexcept {
        if(explicitStop) {stop();return;}
        retained=child.running();
    }
    void stop() noexcept {
        child.stop();retained=false;
        if(logFd>=0) {::close(logFd);logFd=-1;}
        std::error_code ignored;
        if(!configPath.empty()) {
            std::filesystem::remove(configPath,ignored);if(!ignored) configPath.clear();
        }
        if(!runtimePath.empty()) {
            std::filesystem::remove_all(runtimePath,ignored);if(!ignored) runtimePath.clear();
        }
    }
};
} // namespace hftrec::capture::detail
