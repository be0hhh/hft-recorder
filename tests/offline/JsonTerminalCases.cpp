#include "OfflineCase.hpp"
#include "Capture/Coordinator/VenueMultiplexCapture.hpp"
#include "Capture/Session/MarketData/NativeMarketCapture.hpp"
#include "Replay/SessionReplay.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"
#include "Corpus/Recordings/RecordingDiscovery.hpp"
#include "Capture/Session/SessionId.hpp"
#include "cxet/Api/Dispatch/BuildDispatch.hpp"
#include "cxet/Os/ClockSource.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

// Retained native boundary fixture, isolated to this test executable. The
// multiplex, coordinator, JSON serializer, durable manifest and replay owners
// are production code. No socket or real-provider behavior is exercised.
namespace hftrec::capture {
bool NativeMarketCapture::configure(std::span<const NativeCaptureSource> sources,
    const char*,std::string&) noexcept {
  sources_.assign(sources.begin(),sources.end());return true;
}
bool NativeMarketCapture::iterate(std::uint64_t now) noexcept {
  if(committedRows_.load()!=0u) {error_="retained native fixture terminal";return false;}
  for(const auto& source:sources_) {
    replay::BookTickerRow row{};
    row.symbol=source.config->symbols.front();row.exchange=source.config->exchange;row.market=source.config->market;
    row.tsNs=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    row.bidPriceE8=100'000'000;row.askPriceE8=101'000'000;row.bidQtyE8=row.askQtyE8=100'000'000;
    row.captureSeq=row.ingestSeq=1;
    row.arrival.receiveRealtimeNs=row.tsNs;row.arrival.receiveMonotonicNs=now;
    row.arrival.producerEpoch=row.arrival.sourceGeneration=row.arrival.sessionEpoch=1u;
    row.arrival.frameSequence=row.arrival.shardSequence=row.arrival.sourceId=1u;
    row.arrival.flags=replay::EventArrivalApplicationFrame;
    CXET_CHECK(source.sink->appendExternalBookTicker(row)==Status::Ok);
    committedRows_.fetch_add(1u);
  }
  return true;
}
void NativeMarketCapture::shutdown() noexcept {}
bool NativeMarketCapture::connected(std::size_t) const noexcept {return true;}
std::string NativeMarketCapture::error() const noexcept {return error_;}
}
namespace {
struct Fixture {
  std::filesystem::path root=std::filesystem::current_path()/
      ("json-terminal-fixture-"+std::to_string(::getpid())+"-"+
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ~Fixture(){std::error_code error;std::filesystem::remove_all(root,error);}
  std::vector<hftrec::capture::VenueMultiplexJob> jobs() const {
    std::vector<hftrec::capture::VenueMultiplexJob> jobs(2u);
    for(std::size_t i=0;i<jobs.size();++i) {
      auto& job=jobs[i];job.config.exchange="binance";job.config.market="spot";
      job.config.symbols={i?"ETH_USDT":"BTC_USDT"};job.config.outputDir=root;
      job.config.tradesHistoryWarmupSec=0;job.channels.bookTicker=true;
    }
    return jobs;
  }
};
std::string read(const std::filesystem::path& path) {
  std::ifstream input(path);return {(std::istreambuf_iterator<char>(input)),{}};
}
void terminalFanoutDurableManifest() {
  Fixture fixture;hftrec::capture::VenueMultiplexCapture capture;
  CXET_CHECK(capture.start(fixture.jobs())==hftrec::Status::Ok);
  CXET_CHECK(capture.pollOnce() && capture.totalRows()==2u);
  // Terminal immediately after the clean row, inside the stale grace period.
  CXET_CHECK(!capture.pollOnce() && !capture.running());
  CXET_CHECK(capture.finalize()!=hftrec::Status::Ok);
  std::size_t count=0u;
  for(const auto& entry:std::filesystem::recursive_directory_iterator(fixture.root)) {
    if(entry.path().filename()!="manifest.json") continue;
    hftrec::capture::SessionManifest manifest{};
    CXET_CHECK(hftrec::capture::parseManifestJson(read(entry.path()),manifest)==hftrec::Status::Ok);
    CXET_CHECK(!manifest.exactReplayEligible && manifest.sessionStatus=="incomplete");
    CXET_CHECK(!manifest.bookTickerRuntime.lastError.empty());++count;
  }
  CXET_CHECK(count==2u);
}
void terminalReopenCannotRegainExactness() {
  Fixture fixture;hftrec::capture::VenueMultiplexCapture capture;
  CXET_CHECK(capture.start(fixture.jobs())==hftrec::Status::Ok);
  CXET_CHECK(capture.pollOnce());CXET_CHECK(!capture.pollOnce());(void)capture.finalize();
  std::size_t count=0u;
  for(const auto& entry:std::filesystem::recursive_directory_iterator(fixture.root)) {
    if(entry.path().filename()!="manifest.json") continue;
    hftrec::replay::SessionReplay replay;
    (void)replay.open(entry.path().parent_path());
    CXET_CHECK(replay.bookTickers().size()==1u && !replay.exactReplayEligible());
    replay.finalize();CXET_CHECK(!replay.exactReplayEligible());
    hftrec::capture::SessionManifest manifest{};
    CXET_CHECK(hftrec::capture::parseManifestJson(read(entry.path()),manifest)==hftrec::Status::Ok);
    CXET_CHECK(!manifest.exactReplayEligible);
    auto malformed=read(entry.path());
    const auto flag=malformed.find("\"exact_replay_eligible\": false");
    CXET_CHECK(flag!=std::string::npos);
    malformed.replace(flag,std::string{"\"exact_replay_eligible\": false"}.size(),"\"exact_replay_eligible\": \"false\"");
    {std::ofstream output(entry.path());output<<malformed;}
    hftrec::replay::SessionReplay invalid;
    CXET_CHECK(invalid.open(entry.path().parent_path())==hftrec::Status::CorruptData && !invalid.exactReplayEligible());
    ++count;
  }
  CXET_CHECK(count==2u);
}
void optionalEmptyIncidentIsNotClean() {
  Fixture fixture;hftrec::capture::VenueMultiplexCapture capture;
  CXET_CHECK(capture.start(fixture.jobs())==hftrec::Status::Ok);
  CXET_CHECK(capture.pollOnce());capture.requestStop();CXET_CHECK(capture.finalize()==hftrec::Status::Ok);
  std::size_t count=0u;
  for(const auto& entry:std::filesystem::recursive_directory_iterator(fixture.root)) {
    if(entry.path().filename()!="manifest.json") continue;
    hftrec::capture::SessionManifest manifest{};
    CXET_CHECK(hftrec::capture::parseManifestJson(read(entry.path()),manifest)==hftrec::Status::Ok);
    CXET_CHECK(manifest.exactReplayEligible);
    manifest.tradesEnabled=true;manifest.tradesRequiredWhenEnabled=false;
    manifest.tradesRuntime.required=false;manifest.tradesRuntime.state="degraded";
    manifest.tradesRuntime.droppedEventCount=1u;manifest.tradesRuntime.lastError="retained optional capture incident";
    manifest.exactReplayEligible=false;manifest.sessionStatus="complete_degraded";
    {std::ofstream output(entry.path());output<<hftrec::capture::renderManifestJson(manifest);}
    {std::ofstream empty(entry.path().parent_path()/manifest.tradesPath);}
    hftrec::replay::SessionReplay replay;(void)replay.open(entry.path().parent_path());
    CXET_CHECK(replay.trades().empty() && !replay.exactReplayEligible());
    CXET_CHECK(!replay.integritySummary().trades.exactReplayEligible && replay.integritySummary().trades.incidentCount!=0u);
    CXET_CHECK(read(entry.path().parent_path()/"reports"/"integrity_report.json").find("\"exact_replay_eligible\": false")!=std::string::npos);
    ++count;
  }
  CXET_CHECK(count==2u);
}
struct RootEnvironment {
  const char* prior=std::getenv("HFTREC_RECORDINGS_ROOT");
  bool present=prior!=nullptr;
  std::string value=prior?prior:"";
  RootEnvironment(){CXET_CHECK(::unsetenv("HFTREC_RECORDINGS_ROOT")==0);}
  ~RootEnvironment(){if(present) (void)::setenv("HFTREC_RECORDINGS_ROOT",value.c_str(),1);else (void)::unsetenv("HFTREC_RECORDINGS_ROOT");}
};
void recordingPathsPreserveHistoricalPolicy() {
  using namespace hftrec::recordings;
  RootEnvironment env;
  const auto root=std::filesystem::path{"/mnt/d/recordings"};
  CXET_CHECK(defaultRecordingsRoot()==root);
  CXET_CHECK(normalizeRecordingsPath("./recordings/group_a")==root/"group_a");
  CXET_CHECK(normalizeRecordingsPath("apps/hft-recorder/recordings/group_a")==root/"group_a");
  const auto explicitC=std::filesystem::path{"/mnt/c/Users/be0h/PycharmProjects/CXETCPP/apps/hft-recorder/recordings/group_a"};
  CXET_CHECK(normalizeRecordingsPath(explicitC)==root/"group_a");
  CXET_CHECK(normalizeExplicitRecordingsPath(explicitC)==explicitC);
  CXET_CHECK(normalizeExplicitRecordingsPath(R"(C:\Users\be0h\captures)")==std::filesystem::path{"/mnt/c/Users/be0h/captures"});
  CXET_CHECK(normalizeRecordingsPath("d:recordings")==root);
  CXET_CHECK(normalizeExplicitRecordingsPath(R"(D:\recordings\group_a)")==root/"group_a");
  CXET_CHECK(normalizeExplicitRecordingsPath("apps/hft-recorder/recordings/group_a")==root/"group_a");
  CXET_CHECK(normalizeExplicitRecordingsPath({}).empty());
  CXET_CHECK(::setenv("HFTREC_RECORDINGS_ROOT","/isolated/recordings",1)==0);
  CXET_CHECK(defaultRecordingsRoot()==std::filesystem::path{"/isolated/recordings"});
  CXET_CHECK(normalizeExplicitRecordingsPath(explicitC)==explicitC);
}
void recordingSymbolsUseCanonicalOwnerAndSafeEncoding() {
  using namespace hftrec::recordings;
  cxet::initBuildDispatch();
  CXET_CHECK(recordingFolderSymbol("BTW_USDT")=="BTW_USDT");
  CXET_CHECK(recordingFolderSymbol("1000_PEPE_USDT")=="1000_PEPE_USDT");
  CXET_CHECK(recordingFolderSymbol("龙虾_USDT")=="%E9%BE%99%E8%99%BE_USDT");
  CXET_CHECK(recordingFolderSymbol("%E9%BE%99%E8%99%BE_USDT")=="%E9%BE%99%E8%99%BE_USDT");
  CXET_CHECK(recordingFolderSymbol("SBER@MISX")=="SBER%40MISX");
  CXET_CHECK(recordingLocalSymbol("BTWUSDT").empty());
  CXET_CHECK(recordingLocalSymbol("1000%3APEPE%3AUSDT")=="1000_PEPE_USDT");
  CXET_CHECK(recordingFolderSymbol("binance","futures","BTCUSDT")=="BTC_USDT");
  CXET_CHECK(recordingFolderSymbol("okx","futures","btc-usdt-swap")=="BTC_USDT");
  CXET_CHECK(recordingFolderSymbol("binance","spot","BTC_USDT")=="BTC_USDT");
  CXET_CHECK(recordingFolderSymbol("missing","spot","BTC_USDT").empty());
  CXET_CHECK(recordingFolderSymbol("binance","missing","BTC_USDT").empty());
  CXET_CHECK(recordingFolderSymbol("finam","spot","SBER@MISX").empty());
  CXET_CHECK(recordingFolderSymbol("binance","spot","../BTC_USDT").empty());
  CXET_CHECK(recordingFolderSymbol("BTC%ZZ_USDT").empty());
  CXET_CHECK(recordingFolderSymbol(std::string(256u,'A')).empty());
  CXET_CHECK(hftrec::capture::makeSessionId("binance","futures","BTW_USDT",1782141931000000000LL)==
      "1782141931000000000_binance_futures_BTW_USDT");
  std::string longSymbol;
  for(std::size_t i=0u;i<18u;++i) longSymbol+="龙";
  longSymbol+="_USDT";
  const auto expected="1782141931000000000_binance_futures_"+recordingFolderSymbol(longSymbol);
  CXET_CHECK(expected.size()>160u);
  CXET_CHECK(hftrec::capture::makeSessionId("binance","futures",longSymbol,1782141931000000000LL)==expected);
  CXET_CHECK(hftrec::capture::makeSessionId("binance","futures","",1782141931000000000LL).empty());
  CXET_CHECK(hftrec::capture::makeSessionId("","futures","BTC_USDT",1782141931000000000LL).empty());
  CXET_CHECK(hftrec::capture::makeSessionId("binance","futures","BTC_USDT",0).empty());
  CXET_CHECK(hftrec::capture::makeSessionId("binance","futures","BTC%ZZ_USDT",1782141931000000000LL).empty());
  Fixture fixture;
  auto invalidConfig=fixture.jobs().front().config;
  invalidConfig.symbols={std::string(256u,'A')+"_USDT"};
  hftrec::capture::CaptureCoordinator coordinator;
  CXET_CHECK(coordinator.ensureSession(invalidConfig)==hftrec::Status::InvalidArgument);
  CXET_CHECK(!coordinator.lastError().empty() && !std::filesystem::exists(fixture.root));
  auto invalidPathConfig=fixture.jobs().front().config;
  invalidPathConfig.outputDir=std::filesystem::path{std::string{"bad\0path",8u}};
  CXET_CHECK(coordinator.ensureSession(invalidPathConfig)==hftrec::Status::InvalidArgument);
  CXET_CHECK(coordinator.lastError().find("output directory")!=std::string::npos);
  CXET_CHECK(!std::filesystem::exists(fixture.root));
}
const cxet::testing::Case cases[]{
  cxet::testing::Case{"recorder.json_recording_paths_preserve_saved_and_explicit_policy",recordingPathsPreserveHistoricalPolicy},
  cxet::testing::Case{"recorder.json_recording_symbols_use_public_resolver_and_encoding",recordingSymbolsUseCanonicalOwnerAndSafeEncoding},
  cxet::testing::Case{"recorder.json_native_terminal_fanout_is_durable_incomplete",terminalFanoutDurableManifest},
  cxet::testing::Case{"recorder.json_native_terminal_reopen_preserves_negative_evidence",terminalReopenCannotRegainExactness},
  cxet::testing::Case{"recorder.json_optional_empty_channel_retains_capture_incident",optionalEmptyIncidentIsNotClean},
};
}
int main(int argc,char** argv){return cxet::testing::runCases(argc,argv,cases);}
