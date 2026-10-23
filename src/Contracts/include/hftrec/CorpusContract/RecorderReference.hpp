#pragma once
#include "hftrec/Status.hpp"
#include "hft_parser/Reference/CurrentReferenceProtocol.hpp"
#include "hft_parser/Ipc/ReferenceRecordValidation.hpp"
#include "cxet/Primitives/Buf/AsmPrimitives.hpp"
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace hftrec::corpus {
inline constexpr std::uint32_t kRecorderReferenceMagic=0x46524348u;
inline constexpr std::uint16_t kRecorderReferenceSchema=1u;
inline constexpr std::size_t kRecorderReferenceMaximumBytes=128u*1024u*1024u;
inline constexpr std::size_t kRecorderReferenceMaximumSnapshots=65'536u;
enum class RecorderReferencePresence : std::uint8_t {
    Present=1u, Missing, GenerationChanged, ProtocolRejected, IoFailure, CapacityExceeded,
};
struct RecorderReferenceSnapshot final {
    hft_parser::ipc::ReferenceProductIdentity product{};
    hft_parser::ipc::ReferenceDatasetKind dataset{hft_parser::ipc::ReferenceDatasetKind::Instruments};
    RecorderReferencePresence presence{RecorderReferencePresence::Missing};
    // Recorder acquisition is never replaced with a producer observation time.
    // Consumer visibility begins at visibleFromMonotonicNs, and the
    // exact producer row observation/provenance stays unchanged in rowBytes.
    std::uint64_t acquiredMonotonicNs{}, acquiredRealtimeNs{};
    // Zero only while queued. Writer binds these to the actual durable commit.
    std::uint64_t visibleFromCutGeneration{}, visibleFromMonotonicNs{};
    hft_parser::reference_contract::CurrentReferenceHeader producer{};
    std::vector<std::uint8_t> rowBytes{};
    template<class Record> [[nodiscard]] bool record(std::size_t index,Record& out) const noexcept {
        if(presence!=RecorderReferencePresence::Present ||
           dataset!=hft_parser::ipc::referenceDatasetFor<Record>() ||
           producer.snapshot.rowBytes!=sizeof(Record) || index>=producer.snapshot.totalRows ||
           index>=rowBytes.size()/sizeof(Record))return false;
        cxet::bytescan::hftMemcpy(&out,rowBytes.data()+index*sizeof(Record),sizeof(Record));
        return hft_parser::ipc::validReferenceRecord(out) && out.product.exchangeRaw==product.exchangeRaw &&
            out.product.marketRaw==product.marketRaw;
    }
};
namespace reference_detail {
template<class T> inline bool append(std::vector<std::uint8_t>& bytes,const T& value,std::size_t maximum) {
    static_assert(std::is_trivially_copyable_v<T>);
    if(bytes.size()>maximum || sizeof(T)>maximum-bytes.size())return false;
    const auto* data=reinterpret_cast<const std::uint8_t*>(&value);bytes.insert(bytes.end(),data,data+sizeof(T));return true;
}
template<class T> inline bool take(std::span<const std::uint8_t> bytes,std::size_t& position,T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    if(position>bytes.size() || sizeof(T)>bytes.size()-position)return false;
    cxet::bytescan::hftMemcpy(&value,bytes.data()+position,sizeof(T));position+=sizeof(T);return true;
}
inline bool valid(const RecorderReferenceSnapshot& value) noexcept {
    using namespace hft_parser;
    if(!ipc::validReferenceProductIdentity(value.product) || ipc::referenceRecordBytes(value.dataset)==0u ||
       value.presence<RecorderReferencePresence::Present || value.presence>RecorderReferencePresence::CapacityExceeded ||
       !value.acquiredMonotonicNs || !value.acquiredRealtimeNs ||
       (value.visibleFromCutGeneration==0u)!=(value.visibleFromMonotonicNs==0u) ||
       (value.visibleFromMonotonicNs && value.visibleFromMonotonicNs<value.acquiredMonotonicNs))return false;
    if(value.presence!=RecorderReferencePresence::Present)return value.rowBytes.empty();
    return reference_contract::validCurrentReferenceHeader(value.producer) &&
        value.producer.snapshot.product.exchangeRaw==value.product.exchangeRaw &&
        value.producer.snapshot.product.marketRaw==value.product.marketRaw && value.producer.snapshot.dataset==value.dataset &&
        value.rowBytes.size()==static_cast<std::size_t>(value.producer.snapshot.totalRows)*value.producer.snapshot.rowBytes;
}
}
[[nodiscard]] inline Status writeRecorderReferences(std::span<const RecorderReferenceSnapshot> snapshots,
    std::vector<std::uint8_t>& output,std::string& error,std::size_t maximum=kRecorderReferenceMaximumBytes) noexcept {
    try {
        output.clear();error.clear();
        if(maximum>kRecorderReferenceMaximumBytes || snapshots.size()>kRecorderReferenceMaximumSnapshots)return Status::OutOfRange;
        const auto count=static_cast<std::uint32_t>(snapshots.size());
        const auto abi=hft_parser::reference_contract::currentReferenceAbiFingerprint();
        auto add=[&](const auto& value){return reference_detail::append(output,value,maximum);};
        if(!add(kRecorderReferenceMagic)||!add(kRecorderReferenceSchema)||!add(std::uint16_t{})||!add(abi)||!add(count))return Status::OutOfRange;
        for(const auto& snapshot:snapshots) {
            if(!reference_detail::valid(snapshot)) {error="invalid Recorder reference snapshot";return Status::CorruptData;}
            const auto bytes=static_cast<std::uint32_t>(snapshot.rowBytes.size());
            if(!add(snapshot.product)||!add(snapshot.dataset)||!add(snapshot.presence)||!add(std::uint16_t{})||
               !add(snapshot.acquiredMonotonicNs)||!add(snapshot.acquiredRealtimeNs)||
               !add(snapshot.visibleFromCutGeneration)||!add(snapshot.visibleFromMonotonicNs)||!add(snapshot.producer)||!add(bytes)||
               bytes>maximum-output.size())return Status::OutOfRange;
            output.insert(output.end(),snapshot.rowBytes.begin(),snapshot.rowBytes.end());
        }
        return Status::Ok;
    } catch(...) {error="Recorder reference serialization failed";return Status::IoError;}
}
[[nodiscard]] inline Status readRecorderReferences(std::span<const std::uint8_t> bytes,
    std::vector<RecorderReferenceSnapshot>& output,std::string& error) noexcept {
    try {
        output.clear();error.clear();if(bytes.empty())return Status::Ok;
        if(bytes.size()>kRecorderReferenceMaximumBytes)return Status::OutOfRange;
        std::size_t position{};std::uint32_t magic{},count{};std::uint16_t schema{},reserved{};std::uint64_t abi{};
        auto take=[&](auto& value){return reference_detail::take(bytes,position,value);};
        if(!take(magic)||!take(schema)||!take(reserved)||!take(abi)||!take(count)||magic!=kRecorderReferenceMagic ||
           schema!=kRecorderReferenceSchema || reserved || abi!=hft_parser::reference_contract::currentReferenceAbiFingerprint() ||
           count>kRecorderReferenceMaximumSnapshots) {error="Recorder reference schema or ABI rejected";return Status::CorruptData;}
        output.reserve(count);
        for(std::uint32_t i=0u;i<count;++i) {
            RecorderReferenceSnapshot snapshot;std::uint32_t size{};
            if(!take(snapshot.product)||!take(snapshot.dataset)||!take(snapshot.presence)||!take(reserved)||reserved||
               !take(snapshot.acquiredMonotonicNs)||!take(snapshot.acquiredRealtimeNs)||
               !take(snapshot.visibleFromCutGeneration)||!take(snapshot.visibleFromMonotonicNs)||!take(snapshot.producer)||!take(size)||
               size>bytes.size()-position) {error="truncated Recorder reference snapshot";output.clear();return Status::CorruptData;}
            snapshot.rowBytes.assign(bytes.begin()+position,bytes.begin()+position+size);position+=size;
            if(!reference_detail::valid(snapshot)) {error="invalid Recorder reference identity or payload";output.clear();return Status::CorruptData;}
            output.push_back(std::move(snapshot));
        }
        if(position!=bytes.size()) {output.clear();error="trailing Recorder reference data";return Status::CorruptData;}
        return Status::Ok;
    } catch(...) {output.clear();error="Recorder reference decoding failed";return Status::IoError;}
}
// Generic writer callback: prior observations are immutable; new observations
// become visible only with this successful durable Recorder commit.
[[nodiscard]] inline Status bindRecorderReferenceVisibility(std::vector<std::uint8_t>& payload,
    std::span<const std::uint8_t> previous,std::uint64_t generation,std::uint64_t committedMonotonicNs) noexcept {
    if(payload.empty())return previous.empty()?Status::Ok:Status::CorruptData;
    if(!generation || !committedMonotonicNs)return Status::InvalidArgument;
    std::vector<RecorderReferenceSnapshot> current,prior;std::string error;
    auto status=readRecorderReferences(payload,current,error);if(!isOk(status))return status;
    status=readRecorderReferences(previous,prior,error);if(!isOk(status))return status;
    if(current.size()<prior.size())return Status::CorruptData;
    for(std::size_t i=0u;i<current.size();++i) {
        auto& value=current[i];
        if(i<prior.size()) {
            const auto& old=prior[i];
            if(value.product.exchangeRaw!=old.product.exchangeRaw || value.product.marketRaw!=old.product.marketRaw ||
               value.dataset!=old.dataset || value.presence!=old.presence || value.acquiredMonotonicNs!=old.acquiredMonotonicNs ||
               value.acquiredRealtimeNs!=old.acquiredRealtimeNs || value.producer.producerEpoch!=old.producer.producerEpoch ||
               value.producer.generation!=old.producer.generation ||
               cxet::bytescan::hftMemcmp(&value.producer,&old.producer,sizeof(value.producer))!=0 || value.rowBytes!=old.rowBytes ||
               !old.visibleFromCutGeneration || !old.visibleFromMonotonicNs)return Status::CorruptData;
            value.visibleFromCutGeneration=old.visibleFromCutGeneration;value.visibleFromMonotonicNs=old.visibleFromMonotonicNs;
        } else {
            if(value.visibleFromCutGeneration || value.visibleFromMonotonicNs || value.acquiredMonotonicNs>committedMonotonicNs)
                return Status::CorruptData;
            value.visibleFromCutGeneration=generation;value.visibleFromMonotonicNs=committedMonotonicNs;
        }
    }
    return writeRecorderReferences(current,payload,error);
}
} // namespace hftrec::corpus
