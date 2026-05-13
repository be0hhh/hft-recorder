#include "core/arbitrage/BookTickerSpreadMean.hpp"

#include <cmath>

namespace hftrec::arbitrage {

std::vector<BookTickerSpreadMeanPoint> buildRollingBookTickerSpreadMean(
    const std::vector<BookTickerSpreadPoint>& points,
    std::int64_t windowNs,
    double feePenaltyBps) {
    std::vector<BookTickerSpreadMeanPoint> out;
    out.reserve(points.size());
    if (points.empty()) return out;
    if (windowNs <= 0) windowNs = 1;

    std::size_t first = 0u;
    double sum = 0.0;
    for (std::size_t i = 0u; i < points.size(); ++i) {
        sum += points[i].spreadBps;
        const std::int64_t cutoff = points[i].tsNs - windowNs;
        while (first < i && points[first].tsNs < cutoff) {
            sum -= points[first].spreadBps;
            ++first;
        }

        const double count = static_cast<double>(i - first + 1u);
        const double mean = sum / count;
        const double deviation = points[i].spreadBps - mean;

        BookTickerSpreadMeanPoint point{};
        point.tsNs = points[i].tsNs;
        point.meanBps = mean;
        point.deviationBps = deviation;
        point.edgeAfterFeesBps = std::abs(deviation) - feePenaltyBps;
        out.push_back(point);
    }
    return out;
}

}  // namespace hftrec::arbitrage
