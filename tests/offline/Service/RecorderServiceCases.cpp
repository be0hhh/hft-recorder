#include "OfflineCase.hpp"
#include "hftrec/Service/RecorderService.hpp"
#include "hftrec/CorpusContract/RecorderReference.hpp"
#include "../../../src/Service/src/RecorderServiceProtocol.hpp"
#include <fstream>
#include <unistd.h>

namespace {
using namespace hftrec;using namespace hftrec::service;using namespace hftrec::corpus;
void boundedTypedConfigRoundtrip() {
    RecorderServiceConfig config;config.workspaceRoot="/workspace";config.parserTemplate="/profiles/parser.ini";
    config.envPath="/profiles/.env";config.outputRoot="/recordings";config.channelMask=7u;
    config.venues={{"bybit","futures",{},true},{"binance","spot",{"BTCUSDT"},false}};
    config.retainedCompressedBytes=4096u;config.maximumReferenceBytes=1024u*1024u;config.writerQueueRecords=128u;
    detail::Encoder encoded;detail::encodeConfig(encoded,config);CXET_CHECK(encoded.valid);
    detail::Decoder decoded(encoded.bytes);RecorderServiceConfig restored;detail::decodeConfig(decoded,restored);
    CXET_CHECK(decoded.done() && restored.venues.size()==2u && restored.venues[0].fullUniverse);
    CXET_CHECK(restored.venues[1].instruments==config.venues[1].instruments && restored.maximumReferenceBytes==config.maximumReferenceBytes);
    encoded={};config.venues[1].instruments[0]=std::string(129u,'x');detail::encodeConfig(encoded,config);CXET_CHECK(!encoded.valid);
}
void protocolRefusesAbiAndTruncation() {
    const auto packet=detail::packet(detail::Operation::Status,{});detail::Header header{};std::span<const std::uint8_t> payload;
    CXET_CHECK(detail::parsePacket(packet,header,payload));
    auto bad=packet;bad[offsetof(detail::Header,abi)]^=1u;CXET_CHECK(!detail::parsePacket(bad,header,payload));
    bad=packet;bad.pop_back();CXET_CHECK(!detail::parsePacket(bad,header,payload));
    detail::Encoder envelope;envelope.pod(Status::Ok);envelope.string({});envelope.pod(std::uint32_t{7u});
    auto reply=detail::packet(detail::Operation::Block,envelope);std::string error;
    CXET_CHECK(detail::decodeReply(reply,detail::Operation::Block,payload,error)==Status::Ok && payload.size()==sizeof(std::uint32_t));
    reply[offsetof(detail::Header,abi)]^=1u;
    CXET_CHECK(detail::decodeReply(reply,detail::Operation::Block,payload,error)==Status::CorruptData);
    reply=detail::packet(detail::Operation::Status,envelope);
    CXET_CHECK(detail::decodeReply(reply,detail::Operation::Block,payload,error)==Status::CorruptData);
    envelope={};envelope.pod(static_cast<Status>(static_cast<int>(Status::Unknown)+1));envelope.string({});
    reply=detail::packet(detail::Operation::Block,envelope);
    CXET_CHECK(detail::decodeReply(reply,detail::Operation::Block,payload,error)==Status::CorruptData);
    envelope={};envelope.pod(Status::OutOfRange);envelope.string("invalid block");envelope.pod(std::uint8_t{1u});
    reply=detail::packet(detail::Operation::Block,envelope);
    CXET_CHECK(detail::decodeReply(reply,detail::Operation::Block,payload,error)==Status::CorruptData);
}
RecorderReferenceSnapshot missing(std::uint64_t acquired) {
    RecorderReferenceSnapshot value;value.product.exchangeRaw=1u;value.product.marketRaw=1u;
    value.dataset=hft_parser::ipc::ReferenceDatasetKind::Instruments;value.presence=RecorderReferencePresence::Missing;
    value.acquiredMonotonicNs=acquired;value.acquiredRealtimeNs=acquired+1000u;return value;
}
void referenceVisibilityUsesActualDurableCommit() {
    std::vector<RecorderReferenceSnapshot> snapshots{missing(10u)};std::vector<std::uint8_t> payload;std::string error;
    CXET_CHECK(writeRecorderReferences(snapshots,payload,error)==Status::Ok);
    CXET_CHECK(bindRecorderReferenceVisibility(payload,{},1u,20u)==Status::Ok);
    const auto first=payload;CXET_CHECK(readRecorderReferences(payload,snapshots,error)==Status::Ok);
    snapshots.push_back(missing(30u));snapshots.back().presence=RecorderReferencePresence::CapacityExceeded;
    CXET_CHECK(writeRecorderReferences(snapshots,payload,error)==Status::Ok);
    CXET_CHECK(bindRecorderReferenceVisibility(payload,first,2u,40u)==Status::Ok);
    CXET_CHECK(readRecorderReferences(payload,snapshots,error)==Status::Ok && snapshots.size()==2u);
    CXET_CHECK(snapshots[0].visibleFromCutGeneration==1u && snapshots[0].visibleFromMonotonicNs==20u);
    CXET_CHECK(snapshots[1].visibleFromCutGeneration==2u && snapshots[1].visibleFromMonotonicNs==40u);
}
void referenceHistoryCannotRewriteAcquisition() {
    std::vector<RecorderReferenceSnapshot> snapshots{missing(10u)};std::vector<std::uint8_t> payload;std::string error;
    CXET_CHECK(writeRecorderReferences(snapshots,payload,error)==Status::Ok);
    CXET_CHECK(bindRecorderReferenceVisibility(payload,{},1u,20u)==Status::Ok);
    const auto first=payload;CXET_CHECK(readRecorderReferences(payload,snapshots,error)==Status::Ok);
    snapshots[0].acquiredMonotonicNs=9u;CXET_CHECK(writeRecorderReferences(snapshots,payload,error)==Status::Ok);
    CXET_CHECK(bindRecorderReferenceVisibility(payload,first,2u,30u)==Status::CorruptData);
}
}
int main(int argc,char** argv) {
    const cxet::testing::Case cases[]{
        {"recorder.service.typed_bounded_configuration_roundtrip",boundedTypedConfigRoundtrip},
        {"recorder.service.protocol_rejects_abi_and_truncation",protocolRefusesAbiAndTruncation},
        {"recorder.reference.visibility_begins_at_durable_commit",referenceVisibilityUsesActualDurableCommit},
        {"recorder.reference.committed_acquisition_is_immutable",referenceHistoryCannotRewriteAcquisition}};
    return cxet::testing::runCases(argc,argv,cases);
}
