#include "OfflineCase.hpp"
#include "Corpus/Recordings/RecordingGroups.hpp"
#include "Corpus/Recordings/RecordingDiscovery.hpp"
#include "Common/JsonString.hpp"
#include "hftrec/CorpusContract/SessionManifest.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusWriter.hpp"
#include "cxet/Api/Dispatch/BuildDispatch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace recordings=hftrec::recordings;
using Path=std::filesystem::path;
constexpr std::int64_t startNs=1'704'067'200'123'456'789LL;
constexpr auto artifactName="recording_group.json";

std::string read(const Path& path) {
  std::ifstream input(path,std::ios::binary);
  CXET_CHECK(input.good());
  return {(std::istreambuf_iterator<char>(input)),{}};
}
void write(const Path& path,const std::string& value) {
  std::ofstream output(path,std::ios::binary|std::ios::trunc);
  CXET_CHECK(output.good());output<<value;output.close();CXET_CHECK(output.good());
}
using Tree=std::map<std::string,std::string>;
Tree tree(const Path& root) {
  Tree result;
  for(const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
    const auto name=entry.path().lexically_relative(root).generic_string();
    if(entry.is_symlink()) result.emplace(name,"symlink:"+std::filesystem::read_symlink(entry.path()).generic_string());
    else if(entry.is_directory()) result.emplace(name+"/","");
    else if(entry.is_regular_file()) result.emplace(name,read(entry.path()));
  }
  return result;
}
struct Fixture {
  Path arena=std::filesystem::current_path()/
      ("recording-groups-fixture-"+std::to_string(::getpid())+"-"+
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Path root=arena/"recordings";
  Fixture(){cxet::initBuildDispatch();std::filesystem::create_directories(root);}
  ~Fixture(){std::error_code error;std::filesystem::remove_all(arena,error);}
  Path session(const Path& relative,const std::string& id,std::int64_t started=startNs,
               const std::string& exchange="binance",const std::string& status="complete_degraded") const {
    const auto path=root/relative;std::filesystem::create_directories(path);
    hftrec::capture::SessionManifest manifest;
    manifest.sessionId=id;manifest.exchange=exchange;manifest.market="spot";
    manifest.symbols={"BTC_USDT"};manifest.storageSymbol="BTC_USDT";
    manifest.selectedParentDir=path.parent_path().string();
    manifest.startedAtNs=started;manifest.endedAtNs=status=="recording"?0:started+3'000'000'000LL;
    manifest.sessionStatus=status;
    manifest.sessionHealth=hftrec::SessionHealth::Degraded;
    manifest.warningSummary="retained fixture warning";
    manifest.tradesEnabled=manifest.bookTickerEnabled=manifest.candlesEnabled=manifest.candles2Enabled=true;
    manifest.tradesCount=2u;manifest.bookTickerCount=3u;
    manifest.candlesCount=4u;manifest.candles2Count=7u;
    write(path/"manifest.json",hftrec::capture::renderManifestJson(manifest));
    return path;
  }
  recordings::RecordingGroupInfo group(const std::string& name="saved-parent") const {
    auto prefix=name=="saved-parent"?std::string{}:name+"-";
    std::replace(prefix.begin(),prefix.end(),' ','_');
    session(Path{name}/(prefix+"z-session"),prefix+"z-session",startNs+1'000'000'000LL,"bybit");
    session(Path{name}/(prefix+"a-session"),prefix+"a-session");
    const auto result=recordings::discoverRecordings(root);
    CXET_CHECK(result.complete && result.errors.empty());
    const auto found=std::find_if(result.groups.begin(),result.groups.end(),
        [&](const auto& candidate){return candidate.path==root/name;});
    CXET_CHECK(found!=result.groups.end());return *found;
  }
};
template<std::size_t N> void binaryText(std::array<char,N>& output,const std::string& value) {
  CXET_CHECK(value.size()<=output.size());std::copy(value.begin(),value.end(),output.begin());
}
struct PinnedDirectory {
  int fd;
  explicit PinnedDirectory(const Path& path)
      :fd(::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)){CXET_CHECK(fd>=0);}
  ~PinnedDirectory(){if(fd>=0) ::close(fd);}
};
struct BinaryFixture {
  Path path;
  hftrec::corpus::BinaryMarketSource source{};
  hftrec::corpus::BinaryMarketCorpusWriter writer;
  std::string initialPending;
  explicit BinaryFixture(Path directory):path(std::move(directory)) {
    using namespace hftrec::corpus;
    source.sourceId=1u;source.canonicalSymbolId=7u;source.initialSourceGeneration=11u;
    source.venueId=1u;source.marketRaw=2u;source.marketKindRaw=1u;
    source.economicBaseAssetId=1u;source.quoteAssetId=2u;source.priceScale=source.quantityScale=8u;
    binaryText(source.venue,"binance");source.venueBytes=7u;
    binaryText(source.market,"spot");source.marketBytes=4u;
    binaryText(source.canonicalSymbol,"BTC_USDT");source.canonicalSymbolBytes=8u;
    binaryText(source.nativeSymbol,"BTCUSDT");source.nativeSymbolBytes=7u;
    source.directoryFlags=BinaryMarketDirectoryBookTicker;
    source.configuredChannelMask=source.availableChannelMask=source.traderReplayChannelMask=1u;
    source.compatibility[0]=BinaryMarketCompatibility::ExactTraderReplay;
    BinaryMarketWriterConfig config;config.root=path;
    config.sources=std::span<const BinaryMarketSource>{&source,1u};config.producerEpoch=13u;
    config.maximumBytes=32u*1024u;config.segmentTargetBytes=4096u;config.targetDurationNs=10'000'000'000u;
    config.startedReceiveNs=startNs;config.startedMonotonicNs=100u;config.ringCapacity=2u;config.shardCount=1u;
    CXET_CHECK(writer.start(config)==hftrec::Status::Ok);
    initialPending=read(path/"manifest.pending");
  }
  void append(std::uint64_t sequence) {
    using namespace hftrec::corpus;
    BinaryMarketRecord record{};auto& header=record.header;
    header.sourceId=1u;header.sourceGeneration=11u;header.sessionEpoch=17u;
    header.eventSequence=header.frameSequence=header.shardSequence=sequence;
    header.receiveRealtimeNs=startNs+static_cast<std::int64_t>(sequence)*1000;
    header.receiveMonotonicNs=100u+sequence;header.exchangeTimestampNs=header.receiveRealtimeNs-1;
    header.flags=BinaryMarketRecordTraderReplayCompatible;
    header.sourceFlags=BinaryMarketSourceBookTickerBidPresent|BinaryMarketSourceBookTickerAskPresent;
    header.payloadBytes=sizeof(BinaryMarketBookTickerPayload);
    *binaryMarketPayload<BinaryMarketBookTickerPayload>(&record)={100,2,101,3};
    CXET_CHECK(writer.append(record)==hftrec::Status::Ok);
  }
  void seal() {CXET_CHECK(writer.finalize(hftrec::corpus::BinaryMarketStopReason::Requested,startNs+10'000'000'000LL)==hftrec::Status::Ok);}
};
void checkFinalizedBinaryMetadata(const BinaryFixture& fixture) {
  using namespace hftrec::corpus;
  PinnedDirectory directory(fixture.path);CorpusRecordingMetadata metadata;
  std::uint64_t bytesRead=0u;std::string error;
  CXET_CHECK(readCorpusRecordingMetadataAt(directory.fd,metadata,bytesRead,error,
      recordings::kRecordingMaxAggregateInputBytes)==hftrec::Status::Ok);
  CXET_CHECK(bytesRead>0u && bytesRead<=recordings::kRecordingMaxAggregateInputBytes);
  CXET_CHECK(metadata.finalized && metadata.cutGeneration>0u && metadata.manifest.complete==1u);
  CXET_CHECK(metadata.manifest.producerEpoch==13u && metadata.manifest.recordCount==2u && metadata.manifest.sourceCount==1u);
  CXET_CHECK(metadata.sources.size()==1u && metadata.sources.front().sourceId==1u && metadata.sources.front().initialSourceGeneration==11u);
  const auto& source=metadata.sources.front();
  CXET_CHECK(std::string(source.venue.data(),source.venueBytes)=="binance" && std::string(source.market.data(),source.marketBytes)=="spot");
  CXET_CHECK(std::string(source.canonicalSymbol.data(),source.canonicalSymbolBytes)=="BTC_USDT");
  CXET_CHECK(std::string(source.nativeSymbol.data(),source.nativeSymbolBytes)=="BTCUSDT");
  metadata.manifest.producerEpoch=999u;metadata.cutGeneration=888u;
  const auto limited=readCorpusRecordingMetadataAt(directory.fd,metadata,bytesRead,error,sizeof(BinaryMarketManifest)-1u);
  CXET_CHECK(limited!=hftrec::Status::Ok && metadata.manifest.producerEpoch==999u && metadata.cutGeneration==888u);
  CXET_CHECK(bytesRead<sizeof(BinaryMarketManifest));
}
void refused(const recordings::RecordingDiscoveryResult& result) {
  CXET_CHECK(!result.complete && !result.errors.empty());
  CXET_CHECK(result.sessions.empty() && result.groups.empty());
}
const recordings::RecordedSessionInfo& findSession(
    const recordings::RecordingDiscoveryResult& result,const std::string& id) {
  const auto found=std::find_if(result.sessions.begin(),result.sessions.end(),
      [&](const auto& session){return session.sessionId==id;});
  CXET_CHECK(found!=result.sessions.end());return *found;
}
using Fields=std::vector<std::pair<std::string,std::string>>;
Fields artifactFields(const recordings::RecordingGroupInfo& group) {
  const auto quote=[](const std::string& value){return hftrec::json::quote(value);};
  std::string ids="[";
  for(const auto& session:group.sessions) {
    if(ids.size()>1u) ids+=',';
    ids+=quote(session.sessionId);
  }
  ids+=']';
  return {{"schema",quote("hftrec.recording_group")},{"path",quote(group.path.generic_string())},
      {"id",quote(group.id)},{"title",quote(group.title)},{"local_symbol",quote(group.localSymbol)},
      {"folder_symbol",quote(group.folderSymbol)},{"normalized_symbol",quote(group.normalizedSymbol)},
      {"display_time",quote(group.displayTime)},{"search_text",quote(group.searchText)},
      {"started_at_ns",std::to_string(group.startedAtNs)},{"ended_at_ns",std::to_string(group.endedAtNs)},
      {"total_rows",std::to_string(group.totalRows)},{"physical",group.physical?"true":"false"},
      {"session_ids",ids}};
}
std::string object(const Fields& fields,std::size_t omit=static_cast<std::size_t>(-1)) {
  std::string result="{";
  for(std::size_t i=0u;i<fields.size();++i) if(i!=omit) {
    if(result.size()>1u) result+=',';
    result+=hftrec::json::quote(fields[i].first)+":"+fields[i].second;
  }
  return result+"}\n";
}
void loadRefusesUnchanged(Fixture& fixture,const Path& groupPath,const std::string& document) {
  const auto artifact=groupPath/artifactName;write(artifact,document);
  recordings::RecordingGroupInfo output;
  output.id="preserved";output.path=fixture.arena/"preserved";output.sessions.resize(1u);
  output.sessions.front().sessionId="preserved-session";
  std::string error;
  CXET_CHECK(!recordings::loadGroupManifest(fixture.root,groupPath,output,&error) && !error.empty());
  CXET_CHECK(output.id=="preserved" && output.path==fixture.arena/"preserved");
  CXET_CHECK(output.sessions.size()==1u && output.sessions.front().sessionId=="preserved-session");
  CXET_CHECK(read(artifact)==document);
}
void metadataReaderPinsBoundedRegularFiles(Fixture& fixture) {
  using Result=recordings::RecordingMetadataReadResult;
  using Mode=recordings::RecordingMetadataReadMode;
  const auto directory=fixture.root/"metadata-reader-fixture";
  std::filesystem::create_directory(directory);
  const auto document=directory/"document.json";
  write(document,"fixture document");
  std::string output="preserved",error;std::size_t consumed=0u;
  CXET_CHECK(recordings::readRecordingMetadata(fixture.root,document,32u,output,&error,Mode::Document,&consumed)==Result::Ready);
  CXET_CHECK(output=="fixture document" && consumed==output.size());
  output="preserved";
  CXET_CHECK(recordings::readRecordingMetadata(fixture.root,directory/"missing",32u,output,&error)==Result::Missing);
  CXET_CHECK(output=="preserved");
  write(document,std::string(40u,'x'));
  CXET_CHECK(recordings::readRecordingMetadata(fixture.root,document,16u,output,&error)==Result::Refused);
  CXET_CHECK(output=="preserved" && !error.empty());
  const auto fifo=directory/"metadata.fifo";
  CXET_CHECK(::mkfifo(fifo.c_str(),0600)==0);
  CXET_CHECK(recordings::readRecordingMetadata(fixture.root,fifo,64u,output,&error)==Result::Refused);
  CXET_CHECK(output=="preserved" && !error.empty());
  const auto external=fixture.arena/"external-reader.txt";write(external,"external retained bytes");
  const auto link=directory/"metadata-link";std::filesystem::create_symlink(external,link);
  CXET_CHECK(recordings::readRecordingMetadata(fixture.root,link,64u,output,&error)==Result::Refused);
  CXET_CHECK(output=="preserved" && read(external)=="external retained bytes");
  CXET_CHECK(recordings::readRecordingMetadata(fixture.root,external,64u,output,&error)==Result::Refused);
  CXET_CHECK(output=="preserved");
  write(document,"\n \n{\"type\":\"run.result\"}\n"+std::string(128u,'x'));
  CXET_CHECK(recordings::readRecordingMetadata(fixture.root,document,64u,output,&error,Mode::FirstNonemptyLine,&consumed)==Result::Ready);
  CXET_CHECK(output=="{\"type\":\"run.result\"}" && consumed<=64u);
  output="preserved";write(document,std::string(65u,'\n'));
  CXET_CHECK(recordings::readRecordingMetadata(fixture.root,document,64u,output,&error,Mode::FirstNonemptyLine,&consumed)==Result::Refused);
  CXET_CHECK(output=="preserved" && consumed<=64u);
  std::filesystem::remove_all(directory);
}

void utcNamesUseNormalizedSafeSymbols() {
  cxet::initBuildDispatch();
  const auto local=recordings::recordingLocalSymbol("1000%3APEPE%3AUSDT");
  CXET_CHECK(local=="1000_PEPE_USDT");
  const auto folder=recordings::recordingFolderSymbol(local);
  CXET_CHECK(recordings::recordingGroupFolderName(startNs,folder)=="20240101_000000_123456789_1000_PEPE_USDT");
  CXET_CHECK(recordings::recordingGroupFolderName(startNs,"BTC_USDT")=="20240101_000000_123456789_BTC_USDT");
  CXET_CHECK(recordings::recordingDisplayTimestamp(startNs)=="2024-01-01 00:00:00.123456789 UTC");
  CXET_CHECK(recordings::recordingDisplayTimestamp(0)=="1970-01-01 00:00:00.000000000 UTC");
  CXET_CHECK(recordings::recordingGroupFolderName(0,"BTC_USDT")=="19700101_000000_000000000_BTC_USDT");
  CXET_CHECK(recordings::recordingDisplayTimestamp(-1).empty());
  CXET_CHECK(recordings::recordingGroupFolderName(-1,"BTC_USDT").empty());
  CXET_CHECK(recordings::recordingGroupFolderName(startNs,"").empty());
  CXET_CHECK(recordings::recordingGroupFolderName(startNs,"../BTC_USDT").empty());
}
void discoveryPreservesCurrentFieldsAndPhysicalMembership() {
  Fixture fixture;
  const auto group=fixture.group();
  const auto flatZ=fixture.session("flat-z","flat-z");
  const auto flatA=fixture.session("flat-a","flat-a");
  const auto open=fixture.session("recording","recording",startNs,"binance","recording");
  const auto incomplete=fixture.session("closed-incomplete","closed-incomplete",startNs,"binance","incomplete");
  BinaryFixture binaryA(fixture.root/"binary-a");binaryA.append(1u);binaryA.append(2u);binaryA.seal();
  write(binaryA.path/"manifest.pending",binaryA.initialPending);
  checkFinalizedBinaryMetadata(binaryA);
  BinaryFixture binaryB(fixture.root/"binary-parent"/"binary-b");binaryB.append(1u);binaryB.append(2u);binaryB.seal();
  checkFinalizedBinaryMetadata(binaryB);
  const auto run=flatA/"backtests"/"run";
  const auto sweep=flatA/"sweeps"/"saved-sweep";
  std::filesystem::create_directories(run);std::filesystem::create_directories(sweep);
  write(run/"manifest.json",R"({"type":"run.result","legs":[{"session_id":"flat-a"}],"summary":{"total_pnl_e8":0}})");
  write(sweep/"manifest.json",R"({"type":"sweep.result","legs":[{"session_id":"flat-a"}],"rows":{"path":"sweep_results.jsonl"}})");
  write(sweep/"sweep_results.jsonl",R"({"legs":[{"session_id":"flat-a"}]})" "\n");
  Path retained=flatA/"retained-artifacts";
  for(std::size_t i=0u;i<=recordings::kRecordingMaxDepth;++i) retained/="nested";
  std::filesystem::create_directories(retained);write(retained/"manifest.json","retained noncapture bytes");
  const auto before=tree(fixture.root);
  const auto result=recordings::discoverRecordings(fixture.root);
  CXET_CHECK(result.complete && result.errors.empty() && result.sessions.size()==8u && result.groups.size()==2u);
  CXET_CHECK(std::is_sorted(result.sessions.begin(),result.sessions.end(),
      [](const auto& a,const auto& b){return a.path.generic_string()<b.path.generic_string();}));
  const auto& flat=findSession(result,"flat-a");
  CXET_CHECK(flat.path==flatA && flat.manifestPath==flatA/"manifest.json");
  CXET_CHECK(!flat.grouped && flat.groupPath.empty() && flat.groupId.empty() && flat.groupTitle.empty());
  CXET_CHECK(flat.exchange=="binance" && flat.market=="spot" && flat.symbols==std::vector<std::string>{"BTC_USDT"});
  CXET_CHECK(flat.localSymbol=="BTC_USDT" && flat.folderSymbol=="BTC_USDT" && flat.normalizedSymbol=="BTC_USDT");
  CXET_CHECK(flat.tradesCount==2u && flat.bookTickerCount==3u && flat.candleCount==7u && flat.totalRows==16u);
  CXET_CHECK(flat.sessionHealth=="degraded" && flat.warningSummary=="retained fixture warning" && flat.complete);
  CXET_CHECK(flat.startedAtNs==startNs && flat.endedAtNs==startNs+3'000'000'000LL);
  CXET_CHECK(flat.displayTime==recordings::recordingDisplayTimestamp(startNs) && !flat.searchText.empty());
  CXET_CHECK(findSession(result,"flat-z").path==flatZ && !findSession(result,"flat-z").grouped);
  const auto& recording=findSession(result,"recording");
  CXET_CHECK(recording.path==open && recording.endedAtNs==0 && !recording.complete);
  CXET_CHECK(recording.sessionHealth=="degraded" && recording.warningSummary=="retained fixture warning");
  const auto& closedIncomplete=findSession(result,"closed-incomplete");
  CXET_CHECK(closedIncomplete.path==incomplete && closedIncomplete.endedAtNs>0 && closedIncomplete.complete);
  CXET_CHECK(closedIncomplete.sessionHealth=="degraded" && closedIncomplete.warningSummary=="retained fixture warning");
  const auto& firstBinary=findSession(result,"cxm:13:binary-a");
  const auto& secondBinary=findSession(result,"cxm:13:binary-parent/binary-b");
  CXET_CHECK(firstBinary.path==binaryA.path && secondBinary.path==binaryB.path && firstBinary.sessionId!=secondBinary.sessionId);
  CXET_CHECK(firstBinary.manifestPath==binaryA.path/"manifest.bin" && firstBinary.complete && !firstBinary.grouped);
  CXET_CHECK(firstBinary.binaryCorpus && firstBinary.sourceCount==1u && !firstBinary.channelCountsAvailable);
  CXET_CHECK(firstBinary.tradesCount==0u && firstBinary.bookTickerCount==0u && firstBinary.candleCount==0u && firstBinary.totalRows==2u);
  CXET_CHECK(firstBinary.exchange=="binance" && firstBinary.market=="spot" && firstBinary.symbols==std::vector<std::string>{"BTC_USDT"});
  CXET_CHECK(firstBinary.localSymbol=="BTC_USDT" && firstBinary.folderSymbol=="BTC_USDT" && firstBinary.normalizedSymbol=="BTC_USDT");
  CXET_CHECK(firstBinary.startedAtNs==startNs && firstBinary.endedAtNs==startNs+10'000'000'000LL);
  CXET_CHECK(secondBinary.binaryCorpus && secondBinary.totalRows==2u && secondBinary.complete);
  CXET_CHECK(secondBinary.grouped && secondBinary.groupPath==fixture.root/"binary-parent" && secondBinary.groupId=="binary-parent");
  CXET_CHECK(std::is_sorted(result.groups.begin(),result.groups.end(),
      [](const auto& a,const auto& b){return a.path.generic_string()<b.path.generic_string();}));
  const auto physicalPosition=std::find_if(result.groups.begin(),result.groups.end(),
      [&](const auto& candidate){return candidate.path==group.path;});
  CXET_CHECK(physicalPosition!=result.groups.end());const auto& physical=*physicalPosition;
  CXET_CHECK(physical.path==group.path && physical.id=="saved-parent" && physical.physical);
  CXET_CHECK(physical.sessions.size()==2u && physical.sessions[0].sessionId=="a-session" && physical.sessions[1].sessionId=="z-session");
  CXET_CHECK(physical.totalRows==32u && physical.startedAtNs==startNs && physical.endedAtNs==startNs+4'000'000'000LL);
  CXET_CHECK(!physical.title.empty() && !physical.searchText.empty());
  for(const auto& session:physical.sessions) {
    CXET_CHECK(session.grouped && session.groupPath==physical.path && session.groupId==physical.id);
    CXET_CHECK(session.groupTitle==physical.title);
  }
  const auto binaryGroup=std::find_if(result.groups.begin(),result.groups.end(),
      [&](const auto& candidate){return candidate.path==secondBinary.groupPath;});
  CXET_CHECK(binaryGroup!=result.groups.end() && binaryGroup->physical && binaryGroup->totalRows==2u);
  CXET_CHECK(binaryGroup->sessions.size()==1u && binaryGroup->sessions.front().sessionId==secondBinary.sessionId);
  std::string error;CXET_CHECK(recordings::validateRecordingDiscovery(fixture.root,result,&error));
  const auto again=recordings::discoverRecordings(fixture.root);
  CXET_CHECK(again.complete && again.sessions.size()==result.sessions.size());
  for(std::size_t i=0u;i<again.sessions.size();++i) CXET_CHECK(again.sessions[i].path==result.sessions[i].path);
  CXET_CHECK(tree(fixture.root)==before);
  const auto manifest=read(binaryB.path/"manifest.bin");auto corrupted=manifest;
  CXET_CHECK(!corrupted.empty());corrupted.front()=static_cast<char>(corrupted.front()^1);
  write(binaryB.path/"manifest.bin",corrupted);refused(recordings::discoverRecordings(fixture.root));
  write(binaryB.path/"manifest.bin",manifest);CXET_CHECK(tree(fixture.root)==before);
}
void groupArtifactRoundtripRequiresExactCurrentFields() {
  Fixture fixture;const auto group=fixture.group();std::string error;
  CXET_CHECK(recordings::writeGroupManifest(fixture.root,group,&error));
  const auto document=read(group.path/artifactName);
  CXET_CHECK(document.find("hftrec.recording_group")!=std::string::npos);
  CXET_CHECK(document.find("version")==std::string::npos);
  recordings::RecordingGroupInfo loaded;
  CXET_CHECK(recordings::loadGroupManifest(fixture.root,group.path,loaded,&error));
  CXET_CHECK(loaded.path==group.path && loaded.id==group.id && loaded.title==group.title);
  CXET_CHECK(loaded.localSymbol==group.localSymbol && loaded.folderSymbol==group.folderSymbol && loaded.normalizedSymbol==group.normalizedSymbol);
  CXET_CHECK(loaded.displayTime==group.displayTime && loaded.searchText==group.searchText && loaded.physical);
  CXET_CHECK(loaded.startedAtNs==group.startedAtNs && loaded.endedAtNs==group.endedAtNs && loaded.totalRows==group.totalRows);
  CXET_CHECK(loaded.sessions.size()==2u && loaded.sessions[0].sessionId=="a-session" && loaded.sessions[1].sessionId=="z-session");
  const auto fields=artifactFields(group);CXET_CHECK(fields.size()==14u);
  write(group.path/artifactName,object(fields));
  CXET_CHECK(recordings::loadGroupManifest(fixture.root,group.path,loaded,&error));
  for(std::size_t i=0u;i<fields.size();++i) {
    loadRefusesUnchanged(fixture,group.path,object(fields,i));
    auto invalid=fields;invalid[i].second="null";
    loadRefusesUnchanged(fixture,group.path,object(invalid));
    invalid[i].second=i<9u?"0":i<12u?"\"0\"":i==12u?"\"true\"":"{}";
    loadRefusesUnchanged(fixture,group.path,object(invalid));
  }
  for(const auto* key:{"unexpected","version","schema_version"}) {
    auto invalid=fields;invalid.emplace_back(key,"1");
    loadRefusesUnchanged(fixture,group.path,object(invalid));
  }
  auto duplicate=fields;duplicate.push_back(fields.front());
  loadRefusesUnchanged(fixture,group.path,object(duplicate));
  auto versioned=fields;versioned.front().second=hftrec::json::quote("hftrec.recording_group.v1");
  loadRefusesUnchanged(fixture,group.path,object(versioned));
  auto overflow=fields;overflow[11].second="18446744073709551616";
  loadRefusesUnchanged(fixture,group.path,object(overflow));
  overflow[11].second="-1";loadRefusesUnchanged(fixture,group.path,object(overflow));
  auto duplicateIds=fields;duplicateIds[13].second="[\"a-session\",\"a-session\"]";
  loadRefusesUnchanged(fixture,group.path,object(duplicateIds));
  loadRefusesUnchanged(fixture,group.path,"{\"schema\":");
}
void malformedDiscoveryRefusesTheWholeSnapshot() {
  Fixture fixture;const auto good=fixture.session("a-good","a-good");
  const auto bad=fixture.session("z-bad","z-bad");
  const auto valid=read(bad/"manifest.json");
  write(bad/"manifest.json","{}");const auto malformed=tree(fixture.root);
  refused(recordings::discoverRecordings(fixture.root));
  CXET_CHECK(tree(fixture.root)==malformed && std::filesystem::exists(good/"manifest.json"));
  write(bad/"manifest.json",std::string(recordings::kRecordingMaxSessionManifestBytes+1u,' '));
  refused(recordings::discoverRecordings(fixture.root));
  write(bad/"manifest.json",valid);
  auto duplicate=valid;const auto id=duplicate.find("z-bad");CXET_CHECK(id!=std::string::npos);
  duplicate.replace(id,5u,"a-good");write(bad/"manifest.json",duplicate);
  refused(recordings::discoverRecordings(fixture.root));
  write(bad/"manifest.json",valid);
  Path deep="deep";
  for(std::size_t i=0u;i<=recordings::kRecordingMaxDepth;++i) deep/="nested";
  fixture.session(deep,"too-deep");
  const auto before=tree(fixture.root);refused(recordings::discoverRecordings(fixture.root));
  const auto refresh=recordings::rebuildRecordingGroups(fixture.root,true);
  CXET_CHECK(!refresh.complete && refresh.rebuilt.empty() && !refresh.errors.empty());
  CXET_CHECK(tree(fixture.root)==before);
  Fixture live;BinaryFixture pending(live.root/"pending-binary");PinnedDirectory directory(pending.path);
  hftrec::corpus::CorpusRecordingMetadata metadata;metadata.manifest.producerEpoch=999u;metadata.cutGeneration=888u;
  std::uint64_t bytesRead=0u;std::string error;
  CXET_CHECK(hftrec::corpus::readCorpusRecordingMetadataAt(directory.fd,metadata,bytesRead,error,
      recordings::kRecordingMaxAggregateInputBytes)==hftrec::Status::Unknown);
  CXET_CHECK(metadata.manifest.producerEpoch==999u && metadata.cutGeneration==888u);
  CXET_CHECK(bytesRead<=recordings::kRecordingMaxAggregateInputBytes);
  refused(recordings::discoverRecordings(live.root));
  pending.append(1u);CXET_CHECK(pending.writer.publishCut()==hftrec::Status::Ok);
  CXET_CHECK(std::filesystem::exists(pending.path/"cut.bin") && !std::filesystem::exists(pending.path/"manifest.bin"));
  CXET_CHECK(hftrec::corpus::readCorpusRecordingMetadataAt(directory.fd,metadata,bytesRead,error,
      recordings::kRecordingMaxAggregateInputBytes)==hftrec::Status::Unknown);
  CXET_CHECK(metadata.manifest.producerEpoch==999u && metadata.cutGeneration==888u);
  const auto pendingBytes=tree(live.root);refused(recordings::discoverRecordings(live.root));
  CXET_CHECK(tree(live.root)==pendingBytes);
}
void pathsAndSymlinksCannotEscapeRecordingRoot() {
  Fixture fixture;const auto group=fixture.group();std::string error;
  metadataReaderPinsBoundedRegularFiles(fixture);
  CXET_CHECK(recordings::writeGroupManifest(fixture.root,group,&error));
  const auto valid=read(group.path/artifactName);const auto fields=artifactFields(group);
  for(const auto& value:{std::string{"../outside"},(fixture.arena/"outside").generic_string()}) {
    auto invalid=fields;
    invalid[13].second="["+hftrec::json::quote(value)+"]";
    loadRefusesUnchanged(fixture,group.path,object(invalid));
  }
  auto outside=fields;outside[1].second=hftrec::json::quote((fixture.arena/"outside").generic_string());
  loadRefusesUnchanged(fixture,group.path,object(outside));
  write(group.path/artifactName,valid);
  const auto original=tree(fixture.root);
  auto foreignCache=recordings::discoverRecordings(fixture.root);
  CXET_CHECK(foreignCache.complete);foreignCache.sessions.front().path=fixture.arena/"outside";
  CXET_CHECK(!recordings::validateRecordingDiscovery(fixture.root,foreignCache,&error));
  const auto external=fixture.arena/"external-manifest.json";write(external,"external sentinel");
  const auto memberManifest=group.sessions.front().manifestPath;
  const auto memberBytes=read(memberManifest);
  std::filesystem::remove(memberManifest);std::filesystem::create_symlink(external,memberManifest);
  const auto linked=tree(fixture.root);refused(recordings::discoverRecordings(fixture.root));
  CXET_CHECK(tree(fixture.root)==linked && read(external)=="external sentinel");
  std::filesystem::remove(memberManifest);write(memberManifest,memberBytes);
  const auto memberLink=fixture.root/"linked-session";
  std::filesystem::create_directory_symlink(group.sessions.front().path,memberLink);
  refused(recordings::discoverRecordings(fixture.root));std::filesystem::remove(memberLink);
  const auto rootLink=fixture.arena/"root-link";std::filesystem::create_directory_symlink(fixture.root,rootLink);
  refused(recordings::discoverRecordings(rootLink));
  auto escaped=group;escaped.path=fixture.arena/"outside";
  CXET_CHECK(!recordings::writeGroupManifest(fixture.root,escaped,&error));
  CXET_CHECK(!std::filesystem::exists(escaped.path));
  CXET_CHECK(tree(fixture.root)==original);
}
void rebuildChangesOnlyGroupMetadataAtExistingPaths() {
  Fixture fixture;fixture.group("old saved parent");fixture.group("another-parent");
  const auto flat=fixture.session("old-flat-session","old-flat-session");
  write(fixture.root/"retained-note.txt","unchanged foreign artifact");
  const auto before=tree(fixture.root);
  const auto plan=recordings::rebuildRecordingGroups(fixture.root);
  CXET_CHECK(plan.complete && plan.errors.empty() && plan.planned.size()==2u && plan.rebuilt.empty());
  CXET_CHECK(tree(fixture.root)==before);
  const auto applied=recordings::rebuildRecordingGroups(fixture.root,true);
  CXET_CHECK(applied.complete && applied.errors.empty() && applied.rebuilt.size()==2u);
  const auto after=tree(fixture.root);
  for(const auto& [path,value]:before) {
    const auto found=after.find(path);CXET_CHECK(found!=after.end() && found->second==value);
  }
  CXET_CHECK(after.size()==before.size()+2u && std::filesystem::exists(flat/"manifest.json"));
  CXET_CHECK(std::filesystem::exists(fixture.root/"old saved parent"/artifactName));
  CXET_CHECK(std::filesystem::exists(fixture.root/"another-parent"/artifactName));
  const auto result=recordings::discoverRecordings(fixture.root);
  CXET_CHECK(result.complete && result.groups.size()==2u && !findSession(result,"old-flat-session").grouped);
}
void groupReplacementPreservesPriorArtifactOnRefusal() {
  Fixture fixture;auto group=fixture.group();std::string error;
  CXET_CHECK(recordings::writeGroupManifest(fixture.root,group,&error));
  const auto before=tree(fixture.root);
  group.title="Updated retained title";
  CXET_CHECK(recordings::writeGroupManifest(fixture.root,group,&error));
  recordings::RecordingGroupInfo loaded;
  CXET_CHECK(recordings::loadGroupManifest(fixture.root,group.path,loaded,&error) && loaded.title==group.title);
  const auto after=tree(fixture.root);
  CXET_CHECK(after.size()==before.size());
  for(const auto& [path,value]:before) if(path!=(Path{"saved-parent"}/artifactName).generic_string()) {
    const auto found=after.find(path);CXET_CHECK(found!=after.end() && found->second==value);
  }
  auto invalid=group;invalid.sessions.push_back(invalid.sessions.front());
  CXET_CHECK(!recordings::writeGroupManifest(fixture.root,invalid,&error));
  CXET_CHECK(tree(fixture.root)==after);
  {
    struct DirectoryLock {
      int fd;
      ~DirectoryLock(){if(fd>=0) {::flock(fd,LOCK_UN);::close(fd);}}
    } lock{::open(group.path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    CXET_CHECK(lock.fd>=0 && ::flock(lock.fd,LOCK_EX|LOCK_NB)==0);
    CXET_CHECK(!recordings::writeGroupManifest(fixture.root,group,&error));
    CXET_CHECK(tree(fixture.root)==after);
  }
  const auto artifact=group.path/artifactName;
  std::filesystem::remove(artifact);
  const auto external=fixture.arena/"external-group.json";write(external,"external group sentinel");
  std::filesystem::create_symlink(external,artifact);const auto linked=tree(fixture.root);
  CXET_CHECK(!recordings::writeGroupManifest(fixture.root,group,&error));
  CXET_CHECK(read(external)=="external group sentinel" && tree(fixture.root)==linked);
}
void validArtifactGrowthRefreshesWithoutTrustingStaleMembership() {
  Fixture fixture;const auto prior=fixture.group();std::string error;
  CXET_CHECK(recordings::writeGroupManifest(fixture.root,prior,&error));
  const auto cached=recordings::discoverRecordings(fixture.root);CXET_CHECK(cached.complete);
  const auto artifact=prior.path/artifactName;const auto original=read(artifact);
  const auto newSession=fixture.session(Path{"saved-parent"}/"new-session","new-session",startNs+2'000'000'000LL);
  const auto grown=recordings::discoverRecordings(fixture.root);
  CXET_CHECK(grown.complete && grown.errors.empty() && grown.sessions.size()==3u && grown.groups.size()==1u);
  CXET_CHECK(grown.groups.front().sessions.size()==3u && grown.groups.front().totalRows==48u);
  CXET_CHECK(grown.refreshRequired==std::vector<Path>{prior.path});
  CXET_CHECK(recordings::validateRecordingDiscovery(fixture.root,cached,&error));
  CXET_CHECK(findSession(grown,"new-session").path==newSession && read(artifact)==original);
  const auto before=tree(fixture.root);
  CXET_CHECK(!recordings::writeGroupManifest(fixture.root,prior,&error));
  CXET_CHECK(tree(fixture.root)==before);
  const auto dry=recordings::rebuildRecordingGroups(fixture.root);
  CXET_CHECK(dry.complete && dry.errors.empty() && dry.rebuilt.empty() && tree(fixture.root)==before);
  const auto refreshed=recordings::rebuildRecordingGroups(fixture.root,true);
  CXET_CHECK(refreshed.complete && refreshed.errors.empty() && refreshed.rebuilt==std::vector<Path>{prior.path});
  const auto current=recordings::discoverRecordings(fixture.root);
  CXET_CHECK(current.complete && current.refreshRequired.empty() && current.groups.front().sessions.size()==3u);
  CXET_CHECK(findSession(current,"a-session").sessionId=="a-session" && findSession(current,"z-session").sessionId=="z-session");
  const auto after=tree(fixture.root);
  CXET_CHECK(!recordings::writeGroupManifest(fixture.root,prior,&error) && tree(fixture.root)==after);
  auto malformed=artifactFields(current.groups.front());malformed.emplace_back("unknown", "true");
  const auto invalidDocument=object(malformed);write(artifact,invalidDocument);
  refused(recordings::discoverRecordings(fixture.root));
  const auto rejected=recordings::rebuildRecordingGroups(fixture.root,true);
  CXET_CHECK(!rejected.complete && rejected.rebuilt.empty() && !rejected.errors.empty());
  CXET_CHECK(!recordings::writeGroupManifest(fixture.root,current.groups.front(),&error));
  CXET_CHECK(read(artifact)==invalidDocument);
}
const cxet::testing::Case cases[]{
  {"recorder.groups_utc_names_use_normalized_safe_symbols",utcNamesUseNormalizedSafeSymbols},
  {"recorder.groups_discovery_preserves_fields_and_physical_membership",discoveryPreservesCurrentFieldsAndPhysicalMembership},
  {"recorder.groups_artifact_requires_exact_current_fields",groupArtifactRoundtripRequiresExactCurrentFields},
  {"recorder.groups_malformed_discovery_refuses_whole_snapshot",malformedDiscoveryRefusesTheWholeSnapshot},
  {"recorder.groups_paths_and_symlinks_cannot_escape_root",pathsAndSymlinksCannotEscapeRecordingRoot},
  {"recorder.groups_rebuild_changes_only_metadata_at_existing_paths",rebuildChangesOnlyGroupMetadataAtExistingPaths},
  {"recorder.groups_replacement_preserves_prior_artifact_on_refusal",groupReplacementPreservesPriorArtifactOnRefusal},
  {"recorder.groups_growth_refreshes_valid_stale_membership",validArtifactGrowthRefreshesWithoutTrustingStaleMembership},
};
}
int main(int argc,char** argv){return cxet::testing::runCases(argc,argv,cases);}
