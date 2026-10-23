#include "OfflineCase.hpp"
#include "Capture/Coordinator/RecorderCaptureSession.hpp"
#include <string>

namespace {
using namespace hftrec;
using namespace hftrec::capture;
constexpr auto parserTemplate=
  "[runtime]\nca_file=/etc/ssl/certs/ca-certificates.crt\n"
  "[venue.Bybit/Futures]\nreference_binding=ref\nsnapshot_binding=snapshot\n"
  "[venue.Binance/Futures]\nreference_binding=unused\n"
  "[connection.ref]\nvenue=Bybit/Futures\ndomain=Reference\nnative=LinearV5/Rest/Json\noperations=Get/ExchangeInfo\n"
  "[connection.market]\nvenue=Bybit/Futures\ndomain=MarketData\nnative=LinearV5/Ws/Json\n"
  "[connection.snapshot]\nvenue=Bybit/Futures\ndomain=MarketData\nnative=LinearV5/Rest/Json\n"
  "[connection.unused]\nvenue=Binance/Futures\ndomain=MarketData\nnative=FapiStreamsUnversioned/Ws/Json\n";
RecorderCaptureSessionConfig config(bool all=false) {
  RecorderCaptureSessionConfig out;
  out.venues.push_back({"Bybit","Futures",{"BTC_USDT","ETH_USDT"},all});
  if(all)out.venues[0].instruments.clear();
  out.channelMask=3;return out;
}
void exactBatchSelection() {
  auto selection=config();std::string output,error;
  CXET_CHECK(renderRecorderParserConfig(parserTemplate,selection,output,error)==Status::Ok);
  CXET_CHECK(output.find("[venue.Bybit/Futures]")!=std::string::npos);
  CXET_CHECK(output.find("whitelist_instruments=BTC_USDT,ETH_USDT")!=std::string::npos);
  CXET_CHECK(output.find("[venue.Binance/Futures]")==std::string::npos);
  CXET_CHECK(output.find("[connection.unused]")==std::string::npos);
  CXET_CHECK(output.find("[connection.snapshot]")==std::string::npos);
  CXET_CHECK(output.find("[connection.ref]")!=std::string::npos);
  CXET_CHECK(output.find("[connection.market]")!=std::string::npos);
  CXET_CHECK(output.find("streams=")==std::string::npos);
}
void allSelectionDropsOldWhitelist() {
  auto selection=config(true);std::string output,error;
  std::string text=parserTemplate;
  const auto position=text.find("reference_binding=ref");
  text.insert(position,"whitelist_instruments=OLD_USDT\n");
  CXET_CHECK(renderRecorderParserConfig(text,selection,output,error)==Status::Ok);
  CXET_CHECK(output.find("whitelist_instruments=")==std::string::npos);
  CXET_CHECK(output.find("universe_scope=all")!=std::string::npos);
}
void unsupportedSelectionRefuses() {
  auto selection=config();selection.venues[0].exchange="Absent";
  std::string output,error;
  CXET_CHECK(renderRecorderParserConfig(parserTemplate,selection,output,error)!=Status::Ok);
  CXET_CHECK(output.empty() && !error.empty());
  selection=config();selection.venues[0].instruments={"BTC_USDT,BAD"};
  CXET_CHECK(renderRecorderParserConfig(parserTemplate,selection,output,error)!=Status::Ok);
  CXET_CHECK(output.empty());
  for(const auto* retired:{"streams", "bbo_binding", "trade_binding", "depth_binding"}) {
    std::string oldTemplate=parserTemplate;
    oldTemplate += std::string{retired}+"=legacy\n";
    CXET_CHECK(renderRecorderParserConfig(oldTemplate,config(),output,error)==Status::CorruptData);
    CXET_CHECK(output.empty() && error.find("removed")!=std::string::npos);
  }
}
}
int main(int argc,char** argv) {
  const cxet::testing::Case cases[]{
    cxet::testing::Case{"capture.parser_batch_exact_selection",exactBatchSelection},
    cxet::testing::Case{"capture.parser_batch_all_universe",allSelectionDropsOldWhitelist},
    cxet::testing::Case{"capture.parser_batch_unsupported_refuses",unsupportedSelectionRefuses}};
  return cxet::testing::runCases(argc,argv,cases);
}
