#include "RecorderReferenceCapture.hpp"
#include "hftrec/CorpusContract/RecorderReference.hpp"
#include "hft_parser/Reference/CurrentReferenceClient.hpp"
#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>
#include <set>
#include <time.h>

namespace hftrec::capture {
namespace {
std::uint64_t clockNs(clockid_t clock) noexcept {
    timespec now{};if(::clock_gettime(clock,&now)!=0 || now.tv_sec<0 || now.tv_nsec<0)return 0u;
    return static_cast<std::uint64_t>(now.tv_sec)*1'000'000'000u+static_cast<std::uint64_t>(now.tv_nsec);
}
corpus::RecorderReferencePresence presenceFor(hft_parser::reference::CurrentReferenceStatus status) {
    using S=hft_parser::reference::CurrentReferenceStatus;using P=corpus::RecorderReferencePresence;
    switch(status) {case S::Ready:return P::Present;case S::Missing:return P::Missing;
        case S::GenerationChanged:return P::GenerationChanged;case S::ProtocolRejected:return P::ProtocolRejected;
        case S::CapacityExceeded:return P::CapacityExceeded;case S::IoFailure:return P::IoFailure;}
    return P::ProtocolRejected;
}
}
RecorderReferenceCapture::~RecorderReferenceCapture() noexcept {stop();}
void RecorderReferenceCapture::stop() noexcept {stop_.store(true,std::memory_order_release);if(worker_.joinable())worker_.join();}
Status RecorderReferenceCapture::start(const std::filesystem::path& runtime,std::span<const corpus::BinaryMarketSource> sources,
    BufferedCorpusWriter& writer,std::uint64_t maximumBytes) noexcept {
    if(worker_.joinable() || maximumBytes<1024u*1024u || maximumBytes>corpus::kRecorderReferenceMaximumBytes)return Status::InvalidArgument;
    try {
        std::set<std::pair<std::uint8_t,std::uint8_t>> identities;
        for(const auto& source:sources) {
            if(source.venueId==0u || source.venueId>255u || source.marketRaw==0u)return Status::CorruptData;
            identities.emplace(static_cast<std::uint8_t>(source.venueId),source.marketRaw);
        }
        if(identities.size()>64u)return Status::OutOfRange;
        stop_.store(false,std::memory_order_release);status_.store(Status::Ok,std::memory_order_release);
        worker_=std::thread([this,runtime,identities=std::move(identities),&writer,maximumBytes] {
            try {
                using Dataset=hft_parser::ipc::ReferenceDatasetKind;
                constexpr Dataset datasets[]{Dataset::Instruments,Dataset::Ticker24h,Dataset::DeliveryBasis,
                    Dataset::CentralBankRate,Dataset::CentralBankDecision};
                std::uint64_t awaitingGeneration{};std::size_t awaitingCount{};
                auto nextAttempt=std::chrono::steady_clock::now();
                while(!stop_.load(std::memory_order_acquire)) {
                    if(std::chrono::steady_clock::now()<nextAttempt) {std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}
                    nextAttempt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
                    const auto cut=writer.latestCut();
                    if(awaitingGeneration && (!cut || cut->generation<=awaitingGeneration))continue;
                    std::vector<corpus::RecorderReferenceSnapshot> history;std::string error;
                    if(cut && !isOk(corpus::readRecorderReferences(cut->referencePayload,history,error))) {status_.store(Status::CorruptData);return;}
                    if(awaitingCount && history.size()<awaitingCount)continue;
                    awaitingGeneration=0u;awaitingCount=0u;
                    const auto oldCount=history.size();std::size_t byteBudget=cut?cut->referencePayload.size():20u;
                    for(const auto& [exchange,market]:identities)for(const auto dataset:datasets) {
                        if(stop_.load(std::memory_order_acquire))break;
                        hft_parser::ipc::ReferenceProductIdentity product{};product.exchangeRaw=exchange;product.marketRaw=market;
                        hft_parser::reference::CurrentReferenceClient client;
                        const auto acquired=client.readCold(runtime.c_str(),product,dataset);
                        corpus::RecorderReferenceSnapshot snapshot;snapshot.product=product;snapshot.dataset=dataset;
                        snapshot.presence=presenceFor(acquired);snapshot.acquiredMonotonicNs=clockNs(CLOCK_MONOTONIC);
                        snapshot.acquiredRealtimeNs=clockNs(CLOCK_REALTIME);
                        if(!snapshot.acquiredMonotonicNs || !snapshot.acquiredRealtimeNs) {status_.store(Status::IoError);return;}
                        if(snapshot.presence==corpus::RecorderReferencePresence::Present)snapshot.producer=*client.header();
                        const auto last=std::find_if(history.rbegin(),history.rend(),[&](const auto& value) {
                            return value.product.exchangeRaw==exchange && value.product.marketRaw==market && value.dataset==dataset;
                        });
                        if(last!=history.rend() && last->presence==snapshot.presence &&
                           (snapshot.presence!=corpus::RecorderReferencePresence::Present ||
                            (last->producer.producerEpoch==snapshot.producer.producerEpoch && last->producer.generation==snapshot.producer.generation)))continue;
                        // Reserve bounded room for missing/capacity fences even
                        // when a full native snapshot no longer fits history.
                        constexpr std::size_t overhead=sizeof(hft_parser::reference_contract::CurrentReferenceHeader)+64u;
                        const auto reserve=identities.size()*std::size(datasets)*overhead;
                        const auto rows=client.rows();
                        if(history.size()+identities.size()*std::size(datasets)>=corpus::kRecorderReferenceMaximumSnapshots ||
                           byteBudget+overhead+rows.size()+reserve>maximumBytes) {
                            snapshot.presence=corpus::RecorderReferencePresence::CapacityExceeded;snapshot.producer={};
                            if(last!=history.rend() && last->presence==snapshot.presence)continue;
                        } else if(snapshot.presence==corpus::RecorderReferencePresence::Present) {
                            snapshot.rowBytes.resize(rows.size());cxet::bytescan::hftMemcpy(snapshot.rowBytes.data(),rows.data(),rows.size());
                        }
                        if(byteBudget+overhead+snapshot.rowBytes.size()>maximumBytes || history.size()>=corpus::kRecorderReferenceMaximumSnapshots) {
                            status_.store(Status::OutOfRange);return;
                        }
                        byteBudget+=overhead+snapshot.rowBytes.size();history.push_back(std::move(snapshot));
                    }
                    if(history.size()==oldCount)continue;
                    std::vector<std::uint8_t> payload;
                    auto result=corpus::writeRecorderReferences(history,payload,error,static_cast<std::size_t>(maximumBytes));
                    if(isOk(result))result=writer.appendReferencePayload(payload);
                    if(!isOk(result)) {status_.store(result);return;}
                    awaitingGeneration=cut?cut->generation:0u;awaitingCount=history.size();
                }
            } catch(...) {status_.store(Status::IoError,std::memory_order_release);}
        });
        return Status::Ok;
    } catch(...) {return Status::IoError;}
}
} // namespace hftrec::capture
