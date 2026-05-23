#pragma once

#include "hftrec/backtest.hpp"

namespace hftrec::backtest {

using BacktestProgress = hftrec::BacktestProgress;
using BacktestProgressCallback = hftrec::BacktestProgressCallback;
using BacktestRunRequest = hftrec::BacktestRunRequest;
using BacktestRunResult = hftrec::BacktestRunResult;

class BacktestRunner {
  public:
    BacktestRunResult run(const BacktestRunRequest& request,
                          BacktestProgressCallback progressCallback,
                          void* userData) const noexcept;
};

}  // namespace hftrec::backtest