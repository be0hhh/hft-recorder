#include "OfflineCase.hpp"
#include "Capture/Coordinator/RecorderCaptureSession.hpp"
#include <array>
#include <string>
namespace {
using namespace hftrec;using namespace hftrec::capture;using namespace hftrec::corpus;
std::array<BinaryMarketSource,2> directory() {
    std::array<BinaryMarketSource,2> sources{};
    for(std::size_t i=0;i<sources.size();++i) {
        auto& s=sources[i];s.sourceId=i+1;s.venueBytes=5;s.marketBytes=7;s.canonicalSymbolBytes=3;
        const std::string venue="Bybit",market="futures",symbol=i==0?"AAA":"BBB";
        for(std::size_t p=0;p<venue.size();++p)s.venue[p]=venue[p];
        for(std::size_t p=0;p<market.size();++p)s.market[p]=market[p];
        for(std::size_t p=0;p<symbol.size();++p)s.canonicalSymbol[p]=symbol[p];
        s.availableChannelMask=3;
    }
    return sources;
}
RecorderCaptureSessionConfig config() {
    RecorderCaptureSessionConfig config;config.venues={{"bybit","futures",{"AAA"},false}};config.channelMask=2;return config;
}
void partialSelectionChangesProducerFeeds() {
    auto sources=directory();std::vector<RecorderSubscriptionChange> changes;std::string error;
    CXET_CHECK(planRecorderSubscriptionChanges(config(),sources,changes,error)==Status::Ok && changes.size()==3);
    for(const auto& change:changes) CXET_CHECK(!change.add);
    CXET_CHECK(changes[0].sourceId==1 && changes[0].channel==BinaryMarketChannel::BookTicker);
}
void fullUniverseAddsMissingChannelOnly() {
    auto sources=directory();sources[0].availableChannelMask=2;sources[1].availableChannelMask=3;
    auto desired=config();desired.venues[0].instruments.clear();desired.venues[0].fullUniverse=true;desired.channelMask=3;
    std::vector<RecorderSubscriptionChange> changes;std::string error;
    CXET_CHECK(planRecorderSubscriptionChanges(desired,sources,changes,error)==Status::Ok && changes.size()==1);
    CXET_CHECK(changes[0].sourceId==1 && changes[0].channel==BinaryMarketChannel::BookTicker && changes[0].add);
}
void unknownDynamicInstrumentRefusesWholePlan() {
    auto sources=directory();auto desired=config();desired.venues[0].instruments={"MISSING"};
    std::vector<RecorderSubscriptionChange> changes;std::string error;
    CXET_CHECK(planRecorderSubscriptionChanges(desired,sources,changes,error)==Status::Unimplemented && changes.empty());
    CXET_CHECK(!error.empty());
}
}
int main(int argc,char** argv) {
    const cxet::testing::Case cases[]{
        cxet::testing::Case{"capture.dynamic_selection_removes_only_unwanted_feeds",partialSelectionChangesProducerFeeds},
        cxet::testing::Case{"capture.dynamic_full_universe_adds_only_missing_feed",fullUniverseAddsMissingChannelOnly},
        cxet::testing::Case{"capture.dynamic_unknown_instrument_refuses_before_any_command",unknownDynamicInstrumentRefusesWholePlan}};
    return cxet::testing::runCases(argc,argv,cases);
}
