#include "gui/viewmodels/TradingControlViewModel.hpp"

#include <QByteArray>
#include <QMetaObject>

#include <algorithm>
#include <cstdint>
#include <utility>

#if HFTREC_WITH_CXET
#include "api/trading/TradingControl.hpp"
#endif

namespace hftrec::gui {
namespace {

QString cstr(const char* value) {
    return value == nullptr || value[0] == '\0' ? QString{} : QString::fromUtf8(value);
}

QString requestErrorText(const char* err, const QString& fallback) {
    const QString text = cstr(err).trimmed();
    return text.isEmpty() ? fallback : text;
}

#if HFTREC_WITH_CXET
cxet::api::trading::TradingControlConfig makeConfig(const QString& symbol, int apiSlot) noexcept {
    cxet::api::trading::TradingControlConfig config{};
    config.exchange = canon::kExchangeIdBinance;
    config.market = canon::kMarketTypeFutures;
    config.apiSlot = static_cast<std::uint8_t>(apiSlot <= 0 ? 1 : apiSlot);
    const QByteArray bytes = symbol.toUtf8();
    config.symbol.copyFrom(bytes.constData());
    config.envPath = ".env";
    return config;
}
#endif

}  // namespace

TradingControlViewModel::TradingControlViewModel(QObject* parent) : QObject(parent) {}

TradingControlViewModel::~TradingControlViewModel() {
    hardStopAll();
    stopActionThread_();
}

bool TradingControlViewModel::cxetAvailable() const noexcept {
#if HFTREC_WITH_CXET
    return true;
#else
    return false;
#endif
}

void TradingControlViewModel::setSymbol(const QString& symbol) {
    const QString next = symbol.trimmed().toUpper();
    if (next.isEmpty() || next == symbol_) return;
    symbol_ = next;
    instrumentInfoLoaded_ = false;
    instrumentInfoText_ = QStringLiteral("not loaded");
    emit symbolChanged();
    emit instrumentInfoChanged();
}

void TradingControlViewModel::setApiSlot(int apiSlot) {
    const int next = std::max(1, apiSlot);
    if (next == apiSlot_) return;
    apiSlot_ = next;
    emit apiSlotChanged();
}

void TradingControlViewModel::fetchInstrumentInfo() {
#if HFTREC_WITH_CXET
    runAsync_([this] {
        if (!configureCxet_()) return;
        char err[192]{};
        const bool ok = cxet::api::trading::fetchInstrumentInfo(err, sizeof(err));
        (void)cxet::api::trading::refreshAccountSnapshot(nullptr, 0u);
        const QString status = ok ? QString{} : requestErrorText(err, QStringLiteral("instrument info failed"));
        QMetaObject::invokeMethod(this, [this, status] {
            refreshSnapshot();
            if (!status.isEmpty()) setStatus_(status);
        }, Qt::QueuedConnection);
    });
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::refreshSymbolConfig() {
#if HFTREC_WITH_CXET
    runAsync_([this] {
        if (!configureCxet_()) return;
        char err[192]{};
        const bool ok = cxet::api::trading::refreshSymbolConfig(err, sizeof(err));
        (void)cxet::api::trading::refreshAccountSnapshot(nullptr, 0u);
        const QString status = ok ? QStringLiteral("symbol config loaded")
                                  : requestErrorText(err, QStringLiteral("symbol config failed"));
        QMetaObject::invokeMethod(this, [this, status] {
            refreshSnapshot();
            if (!status.isEmpty()) setStatus_(status);
        }, Qt::QueuedConnection);
    });
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::setLeverage(int leverage) {
#if HFTREC_WITH_CXET
    if (leverage < 1 || leverage > 150) {
        setStatus_(QStringLiteral("leverage must be 1..150"));
        return;
    }
    runAsync_([this, leverage] {
        if (!configureCxet_()) return;
        char err[192]{};
        const bool ok = cxet::api::trading::setLeverage(static_cast<std::uint8_t>(leverage), err, sizeof(err));
        const QString status = ok ? QStringLiteral("leverage changed and confirmed")
                                  : requestErrorText(err, QStringLiteral("leverage change failed"));
        QMetaObject::invokeMethod(this, [this, status] {
            refreshSnapshot();
            if (!status.isEmpty()) setStatus_(status);
        }, Qt::QueuedConnection);
    });
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::setMarginType(const QString& marginType) {
#if HFTREC_WITH_CXET
    const QString next = marginType.trimmed().toUpper();
    if (next != QStringLiteral("CROSSED") && next != QStringLiteral("ISOLATED")) {
        setStatus_(QStringLiteral("margin type must be CROSSED or ISOLATED"));
        return;
    }
    runAsync_([this, next] {
        if (!configureCxet_()) return;
        char err[192]{};
        const QByteArray bytes = next.toUtf8();
        const bool ok = cxet::api::trading::setMarginType(bytes.constData(), err, sizeof(err));
        const QString status = ok ? QStringLiteral("margin changed and confirmed")
                                  : requestErrorText(err, QStringLiteral("margin change failed"));
        QMetaObject::invokeMethod(this, [this, status] {
            refreshSnapshot();
            if (!status.isEmpty()) setStatus_(status);
        }, Qt::QueuedConnection);
    });
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}
void TradingControlViewModel::startBookTicker() {
#if HFTREC_WITH_CXET
    if (!configureCxet_()) return;
    char err[192]{};
    if (!cxet::api::trading::startBookTicker(err, sizeof(err)) && err[0] != '\0') setStatus_(cstr(err));
    refreshSnapshot();
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::stopBookTicker() {
#if HFTREC_WITH_CXET
    cxet::api::trading::stopBookTicker();
    refreshSnapshot();
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::startUserStream() {
#if HFTREC_WITH_CXET
    if (!configureCxet_()) return;
    char err[192]{};
    if (!cxet::api::trading::startUserStream(err, sizeof(err)) && err[0] != '\0') setStatus_(cstr(err));
    char accountErr[192]{};
    if (!cxet::api::trading::refreshAccountSnapshot(accountErr, sizeof(accountErr)) && accountErr[0] != '\0') setStatus_(cstr(accountErr));
    refreshSnapshot();
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::stopUserStream() {
#if HFTREC_WITH_CXET
    cxet::api::trading::stopUserStream();
    refreshSnapshot();
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::startOrderSession() {
#if HFTREC_WITH_CXET
    runAsync_([this] {
        if (!configureCxet_()) return;
        char err[192]{};
        const bool ok = cxet::api::trading::startOrderSession(err, sizeof(err));
        const QString status = ok ? QString{} : requestErrorText(err, QStringLiteral("order session start failed"));
        QMetaObject::invokeMethod(this, [this, status] {
            refreshSnapshot();
            if (!status.isEmpty()) setStatus_(status);
        }, Qt::QueuedConnection);
    });
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::stopOrderSession() {
#if HFTREC_WITH_CXET
    runAsync_([this] {
        cxet::api::trading::stopOrderSession();
        QMetaObject::invokeMethod(this, [this] { refreshSnapshot(); }, Qt::QueuedConnection);
    });
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::hardStopAll() {
#if HFTREC_WITH_CXET
    cxet::api::trading::hardStopTradingControl();
    refreshSnapshot();
#endif
}

void TradingControlViewModel::cancelOrder(const QString& origClientOrderId) {
#if HFTREC_WITH_CXET
    const QString clientId = origClientOrderId.trimmed();
    if (clientId.isEmpty()) {
        setStatus_(QStringLiteral("orig client id is required"));
        return;
    }
    runAsync_([this, clientId] {
        if (!configureCxet_()) return;
        char err[192]{};
        const QByteArray bytes = clientId.toUtf8();
        const bool ok = cxet::api::trading::cancelOrderByClientId(bytes.constData(), err, sizeof(err));
        const QString status = ok ? QString{} : requestErrorText(err, QStringLiteral("cancel failed"));
        QMetaObject::invokeMethod(this, [this, status] {
            refreshSnapshot();
            if (!status.isEmpty()) setStatus_(status);
        }, Qt::QueuedConnection);
    });
#else
    setStatus_(QStringLiteral("CXET is not linked"));
#endif
}

void TradingControlViewModel::refreshSnapshot() {
#if HFTREC_WITH_CXET
    cxet::api::trading::TradingControlSnapshot snap{};
    if (!cxet::api::trading::copyTradingControlSnapshot(&snap)) return;
    const bool oldBookTicker = bookTickerRunning_;
    const bool oldUser = userStreamRunning_;
    const bool oldOrder = orderSessionRunning_;
    const bool oldInstrument = instrumentInfoLoaded_;

    statusText_ = cstr(snap.statusText);
    instrumentInfoText_ = cstr(snap.instrumentInfoText);
    symbolConfigText_ = cstr(snap.symbolConfigText);
    marginTypeText_ = cstr(snap.marginTypeText);
    leverageText_ = cstr(snap.leverageText);
    maxNotionalText_ = cstr(snap.maxNotionalText);
    lastBookTickerText_ = cstr(snap.bookTickerText);
    bidPriceText_ = cstr(snap.bidPriceText);
    bidQtyText_ = cstr(snap.bidQtyText);
    askPriceText_ = cstr(snap.askPriceText);
    askQtyText_ = cstr(snap.askQtyText);
    spreadText_ = cstr(snap.spreadText);
    lastUserEventText_ = cstr(snap.lastUserEventText);
    lastOrderText_ = cstr(snap.lastOrderText);
    lastRateLimitText_ = cstr(snap.lastRateLimitText);
    walletBalanceText_ = cstr(snap.walletBalanceText);
    availableBalanceText_ = cstr(snap.availableBalanceText);
    equityText_ = cstr(snap.equityText);
    unrealizedPnlText_ = cstr(snap.unrealizedPnlText);
    realizedPnlText_ = cstr(snap.realizedPnlText);
    openOrderCountText_ = QString::number(static_cast<qulonglong>(snap.openOrderCount));
    filledTradeCountText_ = QString::number(static_cast<qulonglong>(snap.filledTradeCount));
    canceledOrderCountText_ = QString::number(static_cast<qulonglong>(snap.canceledOrderCount));
    rejectedOrderCountText_ = QString::number(static_cast<qulonglong>(snap.rejectedOrderCount));
    instrumentInfoLoaded_ = snap.instrumentInfoLoaded;
    bookTickerRunning_ = snap.bookTickerRunning;
    userStreamRunning_ = snap.userStreamRunning;
    orderSessionRunning_ = snap.orderSessionRunning;

    emit statusChanged();
    emit symbolConfigChanged();
    emit bookTickerChanged();
    emit userEventChanged();
    emit orderEventChanged();
    emit accountChanged();
    if (oldInstrument != instrumentInfoLoaded_) emit instrumentInfoChanged();
    else emit instrumentInfoChanged();
    if (oldBookTicker != bookTickerRunning_) emit bookTickerRunningChanged();
    if (oldUser != userStreamRunning_) emit userStreamRunningChanged();
    if (oldOrder != orderSessionRunning_) emit orderSessionRunningChanged();
#endif
}

void TradingControlViewModel::runAsync_(std::function<void()> action) {
    if (busy_) return;
    stopActionThread_();
    setBusy_(true);
    actionThread_ = std::thread([this, action = std::move(action)] {
        action();
        QMetaObject::invokeMethod(this, [this] { setBusy_(false); }, Qt::QueuedConnection);
    });
}

void TradingControlViewModel::stopActionThread_() {
    if (actionThread_.joinable()) actionThread_.join();
}

void TradingControlViewModel::setBusy_(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged();
}

void TradingControlViewModel::setStatus_(const QString& text) {
    if (statusText_ == text) return;
    statusText_ = text;
    emit statusChanged();
}

bool TradingControlViewModel::configureCxet_() {
#if HFTREC_WITH_CXET
    char err[192]{};
    const auto config = makeConfig(normalizedSymbol_(), apiSlot_);
    if (cxet::api::trading::initTradingControl(config, err, sizeof(err))) return true;
    const QString text = cstr(err);
    QMetaObject::invokeMethod(this, [this, text] { setStatus_(text); }, Qt::QueuedConnection);
    return false;
#else
    return false;
#endif
}

QString TradingControlViewModel::normalizedSymbol_() const {
    const QString trimmed = symbol_.trimmed().toUpper();
    return trimmed.isEmpty() ? QStringLiteral("BTCUSDT") : trimmed;
}

}  // namespace hftrec::gui
