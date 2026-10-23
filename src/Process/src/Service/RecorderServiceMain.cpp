#include "hftrec/Service/RecorderService.hpp"
#include "cxet/Primitives/Buf/StringView.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stopped=0;
void onSignal(int) noexcept {stopped=1;}
void usage() {
    std::cout<<"hft-recorderd [--runtime-dir ABSOLUTE_PRIVATE_DIR] [--config PROFILE] [--start]\n"
        "Headless Recorder control service. --start explicitly begins configured capture.\n"
        "Profiles use workspaceRoot/parserTemplate/envPath/outputRoot, numeric limits,\n"
        "and venue=exchange|market|* (or comma-separated canonical symbols).\n";
}
}
int main(int argc,char** argv) {
    std::filesystem::path runtime,profile;bool start=false;
    for(int i=1;i<argc;++i) {
        const auto option=cxet::StringView::fromCString(argv[i],4096u);
        if(option=="--help" || option=="-h") {usage();return 0;}
        if(option=="--start") {start=true;continue;}
        if((option=="--runtime-dir" || option=="--config") && i+1<argc) {
            if(option=="--runtime-dir")runtime=argv[++i];else profile=argv[++i];continue;
        }
        std::cerr<<"unknown or incomplete Recorder option\n";usage();return 2;
    }
    if(start && profile.empty()) {std::cerr<<"--start requires --config\n";return 2;}
    try {
        if(runtime.empty()) {
            const auto* base=std::getenv("XDG_RUNTIME_DIR");
            runtime=std::filesystem::path(base&&*base?base:"/tmp")/("hft-recorder-"+std::to_string(::geteuid()));
        }
        std::signal(SIGINT,&onSignal);std::signal(SIGTERM,&onSignal);
        hftrec::service::RecorderServiceServer service;std::string error;
        auto status=service.open(runtime,error);
        if(!hftrec::isOk(status)) {std::cerr<<error<<'\n';return 1;}
        if(!profile.empty()) {
            hftrec::service::RecorderServiceConfig config;
            status=hftrec::service::loadRecorderServiceConfig(profile,config,error);
            if(hftrec::isOk(status))status=service.configure(config,error);
            if(hftrec::isOk(status) && start)status=service.start(error);
            if(!hftrec::isOk(status)) {std::cerr<<error<<'\n';return 1;}
        }
        while(!stopped) {
            status=service.poll(100u,error);
            if(!hftrec::isOk(status)) {std::cerr<<error<<'\n';service.requestStop();return 1;}
        }
        service.requestStop();service.close();return 0;
    } catch(...) {std::cerr<<"Recorder service preparation failed\n";return 1;}
}
