#include "gui/viewmodels/CaptureViewModel.hpp"

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

#include <QVariantMap>

#include <algorithm>

namespace hftrec::gui {

QString CaptureViewModel::detailedCandlesMode() const { return detailedCandlesMode_; }
QVariantList CaptureViewModel::detailedCandlesModeChoices() const { return detail::detailedCandlesModeChoices(); }
int CaptureViewModel::detailedCandlesBasisMaxFutures() const noexcept { return detailedCandlesBasisMaxFutures_; }
QVariantList CaptureViewModel::detailedCandlesBasisCandidateRows() const { return detailedCandlesBasisCandidateRows_; }
QString CaptureViewModel::detailedCandlesBasisStatus() const { return detailedCandlesBasisStatus_; }

void CaptureViewModel::setDetailedCandlesMode(const QString& mode) {
    QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("basis") || normalized == QStringLiteral("chain")) normalized = QStringLiteral("basis_chain");
    if (normalized != QStringLiteral("single") && normalized != QStringLiteral("basis_chain")) normalized = QStringLiteral("pair");
    if (normalized == detailedCandlesMode_) return;
    detailedCandlesMode_ = normalized;
    if (detailedCandlesMode_ == QStringLiteral("single")) {
        detailedCandlesLeg2SymbolsText_.clear();
    } else if (detailedCandlesMode_ == QStringLiteral("basis_chain")) {
        detailedCandlesVenueKey_ = QStringLiteral("finam_spot");
        detailedCandlesExchange_ = QStringLiteral("finam");
        detailedCandlesMarket_ = QStringLiteral("spot");
        detailedCandlesLeg2VenueKey_ = QStringLiteral("finam_futures");
        if (detailedCandlesSymbolsText_.isEmpty() || !detailedCandlesSymbolsText_.contains(QLatin1Char('@'))) {
            detailedCandlesSymbolsText_ = QStringLiteral("SBER@MISX");
        }
        detailedCandlesBasisCandidateRows_.clear();
        detailedCandlesBasisStatus_ = QStringLiteral("Build futures chain candidates");
    }
    saveSettings_();
    emit detailedCandlesChanged();
}

void CaptureViewModel::setDetailedCandlesBasisMaxFutures(int maxFutures) {
    maxFutures = std::clamp(maxFutures, 1, 80);
    if (maxFutures == detailedCandlesBasisMaxFutures_) return;
    detailedCandlesBasisMaxFutures_ = maxFutures;
    saveSettings_();
    emit detailedCandlesChanged();
}

void CaptureViewModel::refreshDetailedCandlesBasisCandidates() {
    QString error;
    detailedCandlesBasisCandidateRows_ = detail::detailedCandlesBasisChainCandidates(detailedCandlesVenueKey_,
                                                                                     detailedCandlesSymbolsText_,
                                                                                     detailedCandlesBasisMaxFutures_,
                                                                                     80,
                                                                                     &error);
    if (!error.isEmpty()) {
        detailedCandlesBasisStatus_ = error;
    } else {
        detailedCandlesBasisStatus_ = QStringLiteral("Chain candidates: %1 selected of %2")
            .arg(detail::enabledBasisChainSymbols(detailedCandlesBasisCandidateRows_).size())
            .arg(detailedCandlesBasisCandidateRows_.size());
    }
    emit detailedCandlesChanged();
}

void CaptureViewModel::setDetailedCandlesBasisCandidateEnabled(int index, bool enabled) {
    if (index < 0 || index >= detailedCandlesBasisCandidateRows_.size()) return;
    QVariantMap row = detailedCandlesBasisCandidateRows_[index].toMap();
    if (row.value(QStringLiteral("enabled")).toBool() == enabled) return;
    row.insert(QStringLiteral("enabled"), enabled);
    detailedCandlesBasisCandidateRows_[index] = row;
    detailedCandlesBasisStatus_ = QStringLiteral("Chain candidates: %1 selected of %2")
        .arg(detail::enabledBasisChainSymbols(detailedCandlesBasisCandidateRows_).size())
        .arg(detailedCandlesBasisCandidateRows_.size());
    emit detailedCandlesChanged();
}

}  // namespace hftrec::gui
