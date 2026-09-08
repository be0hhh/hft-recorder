#include "BacktestViewModel.hpp"

#include <QByteArray>
#include <QStringList>
#include <QVariantMap>

#include "BacktestSessionHelpers.hpp"

namespace hftrec::gui {
namespace {

QString settingKeyToken(const QString& value) {
    return QString::fromLatin1(value.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool isFuturesLikeMarket(const QString& market) {
    const QString normalized = market.trimmed().toLower();
    return normalized == QStringLiteral("futures") ||
           normalized == QStringLiteral("future") ||
           normalized == QStringLiteral("forts") ||
           normalized == QStringLiteral("linear") ||
           normalized == QStringLiteral("swap") ||
           normalized == QStringLiteral("perp") ||
           normalized == QStringLiteral("usdt") ||
           normalized == QStringLiteral("usdc");
}

QString latencySummary(const QVariantMap& row) {
    return QStringLiteral("MD captured | Mkt %1/%2 | Lim %3/%4 | Cxl %5/%6 | User %7/%8")
        .arg(row.value(QStringLiteral("marketOrderLatencyUs")).toString(),
             row.value(QStringLiteral("marketOrderJitterUs")).toString(),
             row.value(QStringLiteral("limitOrderLatencyUs")).toString(),
             row.value(QStringLiteral("limitOrderJitterUs")).toString(),
             row.value(QStringLiteral("cancelOrderLatencyUs")).toString(),
             row.value(QStringLiteral("cancelOrderJitterUs")).toString(),
             row.value(QStringLiteral("userDataLatencyUs")).toString(),
             row.value(QStringLiteral("userDataJitterUs")).toString());
}

}  // namespace

QVariantList BacktestViewModel::legSelectionRows() const {
    QVariantList out;
    const QVariantList rows = sessionLegRowsForPaths_(legSelectionCandidatePaths_(), stagedDisabledSessionLegPaths_);
    for (const QVariant& value : rows) {
        QVariantMap row = value.toMap();
        const QString path = row.value(QStringLiteral("path")).toString();
        row.insert(QStringLiteral("currentEnabled"), row.value(QStringLiteral("enabled")).toBool());
        row.insert(QStringLiteral("stagedEnabled"), !stagedDisabledSessionLegPaths_.contains(path));
        row.insert(QStringLiteral("latencySummary"), latencySummary(row));
        out.push_back(row);
    }
    return out;
}

int BacktestViewModel::selectedLegCount() const {
    return selectedSessionPaths_().size();
}

int BacktestViewModel::candidateLegCount() const {
    return legSelectionCandidatePaths_().size();
}

QString BacktestViewModel::selectedLegSummary() const {
    const QVariantList rows = selectedSessionLegs();
    QStringList labels;
    int enabled = 0;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (!row.value(QStringLiteral("enabled")).toBool()) continue;
        ++enabled;
        if (labels.size() < 3) {
            labels.push_back(QStringLiteral("%1 %2")
                .arg(row.value(QStringLiteral("exchange")).toString(),
                     row.value(QStringLiteral("symbol")).toString()));
        }
    }
    if (rows.isEmpty()) return QStringLiteral("No legs");
    QString suffix = labels.join(QStringLiteral(", "));
    if (enabled > labels.size()) suffix += QStringLiteral(" +%1").arg(enabled - labels.size());
    if (suffix.isEmpty()) suffix = QStringLiteral("none");
    return QStringLiteral("%1 / %2 selected: %3").arg(enabled).arg(static_cast<int>(rows.size())).arg(suffix);
}

void BacktestViewModel::setSessionLegSelectionStaged(const QString& path, bool enabled) {
    const QString normalized = normalizedPath_(path);
    if (normalized.isEmpty()) return;
    const QStringList candidates = legSelectionCandidatePaths_();
    if (!candidates.contains(normalized)) return;

    const bool disabled = stagedDisabledSessionLegPaths_.contains(normalized);
    if (enabled && disabled) {
        stagedDisabledSessionLegPaths_.removeAll(normalized);
    } else if (!enabled && !disabled) {
        stagedDisabledSessionLegPaths_.push_back(normalized);
    } else {
        return;
    }
    stagedDisabledSessionLegPaths_ = normalizedDisabledSessionLegPaths_(stagedDisabledSessionLegPaths_);
}

void BacktestViewModel::setLegSelectionRowsStaged(const QVariantList& rows) {
    if (rows.isEmpty()) return;

    QStringList disabled;
    QStringList seen;
    const QStringList candidates = legSelectionCandidatePaths_();
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        const QString path = normalizedPath_(row.value(QStringLiteral("path")).toString());
        if (path.isEmpty() || !candidates.contains(path) || seen.contains(path)) continue;
        seen.push_back(path);
        if (row.value(QStringLiteral("stagedEnabled"), true).toBool() == false) disabled.push_back(path);
    }
    for (const QString& path : candidates) {
        if (!seen.contains(path) && stagedDisabledSessionLegPaths_.contains(path)) disabled.push_back(path);
    }
    stagedDisabledSessionLegPaths_ = normalizedDisabledSessionLegPaths_(disabled);
}

void BacktestViewModel::setAllSessionLegsSelectionStaged(bool enabled) {
    if (enabled) {
        if (stagedDisabledSessionLegPaths_.isEmpty()) return;
        stagedDisabledSessionLegPaths_.clear();
    } else {
        stagedDisabledSessionLegPaths_ = legSelectionCandidatePaths_();
    }
}

void BacktestViewModel::setFuturesSessionLegsSelectionStaged() {
    QStringList disabled;
    const QStringList candidates = legSelectionCandidatePaths_();
    for (const QString& path : candidates) {
        if (!isFuturesLikeMarket(sessionMarketForPath_(path))) disabled.push_back(path);
    }
    stagedDisabledSessionLegPaths_ = normalizedDisabledSessionLegPaths_(disabled);
}

void BacktestViewModel::resetLegSelectionStaged() {
    if (!pendingLegSelectionSessionId_.trimmed().isEmpty()) {
        loadLegSelectionForCurrentSession_();
        return;
    }
    syncStagedLegSelection_();
}

void BacktestViewModel::cancelLegSelection() {
    const bool hadPendingSession = !pendingLegSelectionSessionId_.trimmed().isEmpty();
    pendingLegSelectionSessionId_.clear();
    deferredLegSelectionRefresh_ = false;
    loadLegSelectionForCurrentSession_();
    if (hadPendingSession) {
        emit selectedSessionChanged();
        emit multiSessionChanged();
        emit legSelectionChanged();
        emit primaryLegChanged();
    }
}

void BacktestViewModel::applyLegSelection() {
    const QString pendingSessionId = pendingLegSelectionSessionId_.trimmed();
    const bool commitPendingSession = !pendingSessionId.isEmpty();
    const QStringList next = normalizedDisabledSessionLegPaths_(stagedDisabledSessionLegPaths_);
    const bool deferredRefresh = deferredLegSelectionRefresh_;
    deferredLegSelectionRefresh_ = false;
    if (!commitPendingSession && disabledSessionLegPaths_ == next) {
        syncStagedLegSelection_();
        emit legSelectionChanged();
        if (deferredRefresh) {
            refreshSessionGateStatus_();
            scheduleRefresh_();
        }
        return;
    }

    bool resultSelectionCleared = false;
    if (!selectedRunId_.isEmpty()) {
        if (RunRecord* oldRecord = mutableRecordForRunId_(selectedRunId_)) clearRecordDetails_(*oldRecord);
        selectedRunId_.clear();
        ++detailsLoadGeneration_;
        selectedDetailsErrorText_.clear();
        setDetailsLoading_(false);
        resultSelectionCleared = true;
    }
    if (commitPendingSession) {
        selectedSessionId_ = pendingSessionId;
        pendingLegSelectionSessionId_.clear();
        manualSessionPath_.clear();
        extraSessionIds_.clear();
        symbolOverride_.clear();
    }
    disabledSessionLegPaths_ = next;
    syncStagedLegSelection_();
    saveLegSelectionForCurrentSession_();
    const bool primaryChanged = normalizeSelectedPrimaryLeg_();
    if (primaryChanged || commitPendingSession) savePersistentConfig_();
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    if (commitPendingSession) emit selectedSessionChanged();
    emit legSelectionChanged();
    emit multiSessionChanged();
    emit primaryLegChanged();
    if (resultSelectionCleared) {
        emit selectionChanged();
        emit selectedResultMetricChanged();
        emit detailsLoadingChanged();
    }
    if (!strategyChanged) emit selectedStrategyChanged();
    if (commitPendingSession) {
        emit symbolChanged();
        if (!resultSelectionCleared) emit detailsLoadingChanged();
    }
    emit canRunChanged();
    refreshSessionGateStatus_();
    scheduleRefresh_();
}

void BacktestViewModel::setSessionLegEnabled(const QString& path, bool enabled) {
    setSessionLegSelectionStaged(path, enabled);
    applyLegSelection();
}

QString BacktestViewModel::legSelectionSettingsKey_() const {
    QString token;
    const QString pendingSessionId = pendingLegSelectionSessionId_.trimmed();
    if (!pendingSessionId.isEmpty()) {
        token = pendingSessionId;
    } else if (!manualSessionPath_.trimmed().isEmpty()) {
        token = QStringLiteral("manual:%1").arg(manualSessionPath_);
    } else {
        token = selectedSessionId_.trimmed();
        if (token.isEmpty()) token = selectedSessionPath();
    }
    if (token.trimmed().isEmpty()) return {};
    return QStringLiteral("backtests/leg_selection/%1/disabled_paths").arg(settingKeyToken(token));
}

QStringList BacktestViewModel::normalizedDisabledSessionLegPaths_(const QStringList& disabledPaths) const {
    QStringList out;
    const QStringList candidates = legSelectionCandidatePaths_();
    for (const QString& value : disabledPaths) {
        const QString path = normalizedPath_(value);
        if (!path.isEmpty() && candidates.contains(path) && !out.contains(path)) out.push_back(path);
    }
    return out;
}

void BacktestViewModel::loadLegSelectionForCurrentSession_() {
    const QString key = legSelectionSettingsKey_();
    if (!pendingLegSelectionSessionId_.trimmed().isEmpty()) {
        stagedDisabledSessionLegPaths_.clear();
        if (!key.isEmpty()) {
            stagedDisabledSessionLegPaths_ = normalizedDisabledSessionLegPaths_(settings_.value(key).toStringList());
        }
        return;
    }
    disabledSessionLegPaths_.clear();
    if (!key.isEmpty()) {
        disabledSessionLegPaths_ = normalizedDisabledSessionLegPaths_(settings_.value(key).toStringList());
    }
    syncStagedLegSelection_();
}

void BacktestViewModel::saveLegSelectionForCurrentSession_() {
    const QString key = legSelectionSettingsKey_();
    if (key.isEmpty()) return;
    settings_.setValue(key, disabledSessionLegPaths_);
}

void BacktestViewModel::syncStagedLegSelection_() {
    stagedDisabledSessionLegPaths_ = normalizedDisabledSessionLegPaths_(disabledSessionLegPaths_);
}

}  // namespace hftrec::gui
