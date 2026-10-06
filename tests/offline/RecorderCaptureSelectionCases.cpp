#include "OfflineCase.hpp"
#include "Capture/Coordinator/RecorderCaptureSession.hpp"
#include <string>

namespace {
using namespace hftrec;
using namespace hftrec::capture;
constexpr auto parserTemplate=
  "[runtime]\nca_file=/etc/ssl/certs/ca-certificates.crt\n"
  "[venue.Bybit/Futures]\nreference_binding=ref\nbbo_binding=bbo\ntrade_binding=trade\ndepth_binding=depth\nsnapshot_binding=snapshot\n"
  "[venue.Binance/Futures]\nreference_binding=unused\ntrade_binding=unused\n"
  "[connection.ref]\nvenue=Bybit/Futures\ndomain=Reference\nnative=LinearV5/Rest/Json\noperations=Get/ExchangeInfo\n"
  "[connection.bbo]\nvenue=Bybit/Futures\ndomain=MarketData\nnative=LinearV5/Ws/Json\nstreams=Bbo\n"
  "[connection.trade]\nvenue=Bybit/Futures\ndomain=MarketData\nnative=LinearV5/Ws/Json\nstreams=Trade\n"
  "[connection.depth]\nvenue=Bybit/Futures\ndomain=MarketData\nnative=LinearV5/Ws/Json\nstreams=OrderBook\n"
  "[connection.snapshot]\nvenue=Bybit/Futures\ndomain=MarketData\nnative=LinearV5/Rest/Json\noperations=Get/OrderBook\n"
  "[connection.unused]\nvenue=Binance/Futures\ndomain=MarketData\nnative=FapiStreamsUnversioned/Ws/Json\nstreams=Trade\n";
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
  CXET_CHECK(output.find("[connection.depth]")==std::string::npos);
  CXET_CHECK(output.find("[connection.ref]")!=std::string::npos);
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
}
}
int main(int argc,char** argv) {
  const cxet::testing::Case cases[]{
    cxet::testing::Case{"capture.parser_batch_exact_selection",exactBatchSelection},
    cxet::testing::Case{"capture.parser_batch_all_universe",allSelectionDropsOldWhitelist},
    cxet::testing::Case{"capture.parser_batch_unsupported_refuses",unsupportedSelectionRefuses}};
  return cxet::testing::runCases(argc,argv,cases);
}
