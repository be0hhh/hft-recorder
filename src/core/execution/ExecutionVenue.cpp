#include <core/execution/ExecutionVenue.hpp>

#include <algorithm>
#include <iterator>

namespace hftrec::execution {

void LiveExecutionStore::onExecutionEvent(const ExecutionEvent& event) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    batch_.events.push_back(event);
}

ExecutionEventBatch LiveExecutionStore::readAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return batch_;
}

ExecutionEventBatch LiveExecutionStore::readRange(std::uint64_t fromTsNs, std::uint64_t toTsNs) const {
    ExecutionEventBatch out{};
    std::lock_guard<std::mutex> lock(mutex_);
    std::copy_if(batch_.events.begin(), batch_.events.end(), std::back_inserter(out.events),
                 [fromTsNs, toTsNs](const ExecutionEvent& event) noexcept {
                     return event.tsNs >= fromTsNs && event.tsNs <= toTsNs;
                 });
    return out;
}

void LiveExecutionStore::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    batch_ = ExecutionEventBatch{};
}

}  // namespace hftrec::execution
