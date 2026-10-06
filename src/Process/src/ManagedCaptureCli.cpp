#include "../../Runtime/src/Capture/Coordinator/RecorderCaptureSession.hpp"
#include <charconv>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hftrec::app {
namespace {
volatile std::sig_atomic_t interrupted=0;
void interruptCapture(int) noexcept {interrupted=1;}
void usage() {
    std::puts("capture --venue EXCHANGE/MARKET [--venue ...] --symbol CANONICAL [--symbol ...] --channels bookticker,trades --duration-sec N --output DIR --max-bytes N");
    std::puts("Use --full-universe instead of --symbol; one Parser and one compressed binary corpus cover the complete selected batch.");
    std::puts("Optional: --parser-config ABSOLUTE_TEMPLATE --env EXISTING_TEMPLATE_SIBLING_ENV --segment-bytes N");
    std::puts("Legacy live form: capture <trades|bookticker|orderbook> [seconds] [output] [exchange] [symbol] [market]");
}
bool positive(std::string_view text,std::uint64_t& result) {
    const auto value=std::from_chars(text.data(),text.data()+text.size(),result);
    return !text.empty() && value.ec==std::errc{} && value.ptr==text.data()+text.size() && result>0u;
}
std::uint16_t channels(std::string_view text) {
    std::uint16_t mask=0u;
    while(!text.empty()) {
        const auto end=text.find(',');const auto token=text.substr(0,end);
        if(token=="bookticker" || token=="bbo") mask|=1u;
        else if(token=="trade" || token=="trades") mask|=2u;
        else if(token=="depth" || token=="orderbook") mask|=4u;
        else if(token=="liquidation") mask|=8u;
        else if(token=="markprice") mask|=16u;
        else if(token=="indexprice") mask|=32u;
        else if(token=="funding") mask|=64u;
        else if(token=="pricelimit") mask|=128u;
        else if(token=="all") mask|=255u;
        else return 0u;
        if(end==std::string_view::npos) break;text.remove_prefix(end+1u);
    }
    return mask;
}
} // namespace
int runManagedCapture(int argc,char** argv) {
    if(argc<2) {usage();return 2;}
    capture::RecorderCaptureSessionConfig config;
    config.workspaceRoot=capture::findRecorderWorkspaceRoot();
    config.parserTemplate=config.workspaceRoot/"apps/hft-parser/parser.ini";
    config.channelMask=3u;
    std::vector<std::string> symbols;bool fullUniverse=false;bool explicitEnv=false;
    std::vector<std::string> positional;
    for(int i=1;i<argc;++i) {
        const std::string_view argument=argv[i];
        if(argument=="--help" || argument=="-h") {usage();return 0;}
        if(argument=="--full-universe") {fullUniverse=true;continue;}
        if(argument.starts_with("--")) {
            if(i+1>=argc) {std::fprintf(stderr,"capture: %s needs a value\n",argv[i]);return 2;}
            const std::string value=argv[++i];
            if(argument=="--venue") {
                const auto slash=value.find('/');
                if(slash==std::string::npos || slash==0u || slash+1u==value.size()) {std::fputs("capture: venue must be exchange/market\n",stderr);return 2;}
                config.venues.push_back({value.substr(0,slash),value.substr(slash+1u),{},false});
            } else if(argument=="--symbol") {
                if(config.venues.empty()) symbols.push_back(value);
                else config.venues.back().instruments.push_back(value);
            }
            else if(argument=="--channels") {config.channelMask=channels(value);if(config.channelMask==0u) {std::fputs("capture: invalid logical channels\n",stderr);return 2;}}
            else if(argument=="--parser-config") config.parserTemplate=std::filesystem::absolute(value);
            else if(argument=="--env") {config.envPath=std::filesystem::absolute(value);explicitEnv=true;}
            else if(argument=="--output") config.outputRoot=std::filesystem::absolute(value);
            else if(argument=="--duration-sec") {if(!positive(value,config.durationSec)) return 2;}
            else if(argument=="--max-bytes") {if(!positive(value,config.maximumBytes)) return 2;}
            else if(argument=="--segment-bytes") {if(!positive(value,config.segmentBytes)) return 2;}
            else {std::fprintf(stderr,"capture: unsupported option %.*s\n",static_cast<int>(argument.size()),argument.data());return 2;}
        } else positional.emplace_back(argument);
    }
    if(!positional.empty()) {
        if(positional.size()>6u || channels(positional[0])==0u) {usage();return 2;}
        config.channelMask=channels(positional[0]);
        if(positional.size()>1u && positional[1]=="all") {
            std::fputs("capture: use repeated --venue selections and --full-universe for one Parser batch\n",stderr);return 2;
        }
        if(positional.size()>1u && !positive(positional[1],config.durationSec)) return 2;
        if(positional.size()>2u) config.outputRoot=std::filesystem::absolute(positional[2]);
        if(config.venues.empty()) config.venues.push_back({positional.size()>3u?positional[3]:"binance",positional.size()>5u?positional[5]:"futures",{},false});
        if(positional.size()>4u) symbols.push_back(positional[4]);
    }
    if(config.venues.empty()) {std::fputs("capture: select at least one --venue exchange/market\n",stderr);return 2;}
    if(fullUniverse && (!symbols.empty() || std::any_of(config.venues.begin(),config.venues.end(),[](const auto& venue){return !venue.instruments.empty();}))) {std::fputs("capture: --full-universe and --symbol are mutually exclusive\n",stderr);return 2;}
    if(!fullUniverse && symbols.empty() && !positional.empty()) symbols.push_back("ETH_USDT");
    for(auto& venue:config.venues) {
        venue.fullUniverse=fullUniverse;
        if(!fullUniverse && venue.instruments.empty()) venue.instruments=symbols;
    }
    if(!explicitEnv) config.envPath=config.parserTemplate.parent_path()/".env";
    capture::RecorderCaptureSession session;std::string error;
    auto status=session.start(config,error);
    if(!isOk(status)) {std::fprintf(stderr,"capture: %s\n",error.c_str());return 1;}
    interrupted=0;
    const auto previousInt=std::signal(SIGINT,interruptCapture),previousTerm=std::signal(SIGTERM,interruptCapture);
    auto nextDashboard=std::chrono::steady_clock::now();
    while(session.snapshot().active) {
        if(interrupted) session.requestStop();
        if(std::chrono::steady_clock::now()>=nextDashboard) {
            const auto snapshot=session.snapshot();
            std::printf("capture: parser=%s sources=%u records=%llu gaps=%llu bytes=%llu/%llu session=%s\n",
                snapshot.connected?"connected":"starting",snapshot.sourceCount,
                static_cast<unsigned long long>(snapshot.writer.recordCount),static_cast<unsigned long long>(snapshot.writer.gapCount),
                static_cast<unsigned long long>(snapshot.writer.projectedBytes),static_cast<unsigned long long>(config.maximumBytes),snapshot.sessionPath.c_str());
            nextDashboard=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    status=session.stop();std::signal(SIGINT,previousInt);std::signal(SIGTERM,previousTerm);
    const auto snapshot=session.snapshot();
    if(!isOk(status) || !snapshot.complete) {std::fprintf(stderr,"capture incomplete: %s\n",snapshot.error.c_str());return 1;}
    std::printf("compressed capture complete: records=%llu sources=%u session=%s\n",
        static_cast<unsigned long long>(snapshot.writer.recordCount),snapshot.sourceCount,snapshot.sessionPath.c_str());return 0;
}
} // namespace hftrec::app
