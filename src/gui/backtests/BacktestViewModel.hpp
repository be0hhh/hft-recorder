#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace hftrec::gui {

class BacktestViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString recordingsRoot READ recordingsRoot CONSTANT)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QString selectedSessionId READ selectedSessionId WRITE setSelectedSessionId NOTIFY selectedSessionChanged)
    Q_PROPERTY(QString selectedSessionPath READ selectedSessionPath NOTIFY selectedSessionChanged)
    Q_PROPERTY(QString sessionPath READ sessionPath WRITE setSessionPath NOTIFY selectedSessionChanged)
    Q_PROPERTY(QString extraSessionIds READ extraSessionIds WRITE setExtraSessionIds NOTIFY multiSessionChanged)
    Q_PROPERTY(QVariantList selectedSessionLegs READ selectedSessionLegs NOTIFY multiSessionChanged)
    Q_PROPERTY(int selectedSessionCount READ selectedSessionCount NOTIFY multiSessionChanged)
    Q_PROPERTY(QString selectedSymbol READ selectedSymbol WRITE setSelectedSymbol NOTIFY symbolChanged)
    Q_PROPERTY(QString backtestsDirectory READ backtestsDirectory NOTIFY selectedSessionChanged)
    Q_PROPERTY(QVariantList strategyChoices READ strategyChoices NOTIFY selectedStrategyChanged)
    Q_PROPERTY(QString selectedStrategy READ selectedStrategy WRITE setSelectedStrategy NOTIFY selectedStrategyChanged)
    Q_PROPERTY(QString configMode READ configMode WRITE setConfigMode NOTIFY configChanged)
    Q_PROPERTY(QVariantList indicatorProfileChoices READ indicatorProfileChoices NOTIFY selectedStrategyChanged)
    Q_PROPERTY(QString selectedIndicatorProfile READ selectedIndicatorProfile WRITE setSelectedIndicatorProfile NOTIFY indicatorProfileChanged)
    Q_PROPERTY(QVariantList configModeChoices READ configModeChoices NOTIFY selectedStrategyChanged)
    Q_PROPERTY(QVariantList strategyParameters READ strategyParameters NOTIFY strategyParametersChanged)
    Q_PROPERTY(QString profileName READ profileName WRITE setProfileName NOTIFY profileChanged)
    Q_PROPERTY(QString pingLatencyUs READ pingLatencyUs WRITE setPingLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString latencySeed READ latencySeed WRITE setLatencySeed NOTIFY latencyChanged)
    Q_PROPERTY(QString marketDataLatencyUs READ marketDataLatencyUs WRITE setMarketDataLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString marketDataJitterUs READ marketDataJitterUs WRITE setMarketDataJitterUs NOTIFY latencyChanged)
    Q_PROPERTY(QString marketOrderLatencyUs READ marketOrderLatencyUs WRITE setMarketOrderLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString marketOrderJitterUs READ marketOrderJitterUs WRITE setMarketOrderJitterUs NOTIFY latencyChanged)
    Q_PROPERTY(QString limitOrderLatencyUs READ limitOrderLatencyUs WRITE setLimitOrderLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString limitOrderJitterUs READ limitOrderJitterUs WRITE setLimitOrderJitterUs NOTIFY latencyChanged)
    Q_PROPERTY(QString cancelOrderLatencyUs READ cancelOrderLatencyUs WRITE setCancelOrderLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString cancelOrderJitterUs READ cancelOrderJitterUs WRITE setCancelOrderJitterUs NOTIFY latencyChanged)
    Q_PROPERTY(QString userDataLatencyUs READ userDataLatencyUs WRITE setUserDataLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString userDataJitterUs READ userDataJitterUs WRITE setUserDataJitterUs NOTIFY latencyChanged)
    Q_PROPERTY(QString initialBalanceUsdt READ initialBalanceUsdt WRITE setInitialBalanceUsdt NOTIFY accountingChanged)
    Q_PROPERTY(bool riskEnabled READ riskEnabled WRITE setRiskEnabled NOTIFY accountingChanged)
    Q_PROPERTY(QString riskMinEquityPct READ riskMinEquityPct WRITE setRiskMinEquityPct NOTIFY accountingChanged)
    Q_PROPERTY(QString riskMinLegEquityPct READ riskMinLegEquityPct WRITE setRiskMinLegEquityPct NOTIFY accountingChanged)
    Q_PROPERTY(QString riskMinLegEquityUsdt READ riskMinLegEquityUsdt WRITE setRiskMinLegEquityUsdt NOTIFY accountingChanged)
    Q_PROPERTY(QString riskMaxPositionUsdt READ riskMaxPositionUsdt WRITE setRiskMaxPositionUsdt NOTIFY accountingChanged)
    Q_PROPERTY(QString riskRateLimitGuardMinRemaining READ riskRateLimitGuardMinRemaining WRITE setRiskRateLimitGuardMinRemaining NOTIFY accountingChanged)
    Q_PROPERTY(bool rateLimitsEnabled READ rateLimitsEnabled WRITE setRateLimitsEnabled NOTIFY rateLimitsChanged)
    Q_PROPERTY(QString makerFeeBps READ makerFeeBps WRITE setMakerFeeBps NOTIFY accountingChanged)
    Q_PROPERTY(QString takerFeeBps READ takerFeeBps WRITE setTakerFeeBps NOTIFY accountingChanged)
    Q_PROPERTY(QString orderLatencyUs READ orderLatencyUs WRITE setOrderLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString cancelLatencyUs READ cancelLatencyUs WRITE setCancelLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString sweepBudget READ sweepBudget WRITE setSweepBudget NOTIFY sweepConfigChanged)
    Q_PROPERTY(QString sweepSeed READ sweepSeed WRITE setSweepSeed NOTIFY sweepConfigChanged)
    Q_PROPERTY(QVariantList sweepCurveLimitChoices READ sweepCurveLimitChoices CONSTANT)
    Q_PROPERTY(QString selectedSweepCurveLimit READ selectedSweepCurveLimit WRITE setSelectedSweepCurveLimit NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList sweepViewChoices READ sweepViewChoices CONSTANT)
    Q_PROPERTY(QString selectedSweepView READ selectedSweepView WRITE setSelectedSweepView NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList sweepMetricChoices READ sweepMetricChoices NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSweepMetric READ selectedSweepMetric WRITE setSelectedSweepMetric NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedSweepDistributionParamChoices READ selectedSweepDistributionParamChoices NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSweepDistributionParam READ selectedSweepDistributionParam WRITE setSelectedSweepDistributionParam NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList runs READ runs NOTIFY runsChanged)
    Q_PROPERTY(int runCount READ runCount NOTIFY runsChanged)
    Q_PROPERTY(QString selectedRunId READ selectedRunId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedJson READ selectedJson NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSummaryJson READ selectedSummaryJson NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedConfigText READ selectedConfigText NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedErrorText READ selectedErrorText NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedEquityPoints READ selectedEquityPoints NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList resultScopeChoices READ resultScopeChoices NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedResultScope READ selectedResultScope WRITE setSelectedResultScope NOTIFY selectedResultScopeChanged)
    Q_PROPERTY(QVariantList selectedResultMetrics READ selectedResultMetrics NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedResultMetricKey READ selectedResultMetricKey WRITE setSelectedResultMetricKey NOTIFY selectedResultMetricChanged)
    Q_PROPERTY(QString selectedResultMetricRatioKey READ selectedResultMetricRatioKey WRITE setSelectedResultMetricRatioKey NOTIFY selectedResultMetricChanged)
    Q_PROPERTY(QVariantList selectedResultMetricSeries READ selectedResultMetricSeries NOTIFY selectedResultMetricChanged)
    Q_PROPERTY(QVariantList resultMetricRatioChoices READ resultMetricRatioChoices NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedSweepRows READ selectedSweepRows NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedSweepCurves READ selectedSweepCurves NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedSweepDistributionBars READ selectedSweepDistributionBars NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsSweep READ selectedIsSweep NOTIFY selectionChanged)
    Q_PROPERTY(bool hasEquityPoints READ hasEquityPoints NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedPreviewLoading READ selectedPreviewLoading NOTIFY previewLoadingChanged)
    Q_PROPERTY(bool selectedDetailsLoaded READ selectedDetailsLoaded NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedDetailsLoading READ selectedDetailsLoading NOTIFY detailsLoadingChanged)
    Q_PROPERTY(QString selectedDetailsErrorText READ selectedDetailsErrorText NOTIFY detailsLoadingChanged)
    Q_PROPERTY(qint64 selectedInitialBalanceE8 READ selectedInitialBalanceE8 NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectedPnlMinE8 READ selectedPnlMinE8 NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectedPnlMaxE8 READ selectedPnlMaxE8 NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY canRunChanged)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)

  public:
    explicit BacktestViewModel(QObject* parent = nullptr);
    ~BacktestViewModel() override;

    QString recordingsRoot() const;
    QVariantList sessions() const;
    QString selectedSessionId() const { return selectedSessionId_; }
    QString selectedSessionPath() const;
    QString sessionPath() const { return selectedSessionPath(); }
    QString extraSessionIds() const { return extraSessionIds_; }
    QVariantList selectedSessionLegs() const;
    int selectedSessionCount() const;
    QString selectedSymbol() const;
    QString backtestsDirectory() const;
    QVariantList strategyChoices() const;
    QString selectedStrategy() const { return selectedStrategy_; }
    QString configMode() const { return configMode_; }
    QVariantList indicatorProfileChoices() const;
    QString selectedIndicatorProfile() const { return selectedIndicatorProfile_; }
    QVariantList configModeChoices() const;
    QVariantList strategyParameters() const;
    QString profileName() const { return profileName_; }
    QString pingLatencyUs() const { return pingLatencyUs_; }
    QString latencySeed() const { return latencySeed_; }
    QString marketDataLatencyUs() const { return marketDataLatencyUs_; }
    QString marketDataJitterUs() const { return marketDataJitterUs_; }
    QString marketOrderLatencyUs() const { return marketOrderLatencyUs_; }
    QString marketOrderJitterUs() const { return marketOrderJitterUs_; }
    QString limitOrderLatencyUs() const { return limitOrderLatencyUs_; }
    QString limitOrderJitterUs() const { return limitOrderJitterUs_; }
    QString cancelOrderLatencyUs() const { return cancelOrderLatencyUs_; }
    QString cancelOrderJitterUs() const { return cancelOrderJitterUs_; }
    QString userDataLatencyUs() const { return userDataLatencyUs_; }
    QString userDataJitterUs() const { return userDataJitterUs_; }
    QString initialBalanceUsdt() const { return initialBalanceUsdt_; }
    bool riskEnabled() const noexcept { return riskEnabled_; }
    QString riskMinEquityPct() const { return riskMinEquityPct_; }
    QString riskMinLegEquityPct() const { return riskMinLegEquityPct_; }
    QString riskMinLegEquityUsdt() const { return riskMinLegEquityUsdt_; }
    QString riskMaxPositionUsdt() const { return riskMaxPositionUsdt_; }
    QString riskRateLimitGuardMinRemaining() const { return riskRateLimitGuardMinRemaining_; }
    bool rateLimitsEnabled() const noexcept { return rateLimitsEnabled_; }
    QString makerFeeBps() const { return makerFeeBps_; }
    QString takerFeeBps() const { return takerFeeBps_; }
    QString orderLatencyUs() const { return marketOrderLatencyUs_; }
    QString cancelLatencyUs() const { return cancelOrderLatencyUs_; }
    QString sweepBudget() const { return sweepBudget_; }
    QString sweepSeed() const { return sweepSeed_; }
    QVariantList sweepCurveLimitChoices() const;
    QString selectedSweepCurveLimit() const { return selectedSweepCurveLimit_; }
    QVariantList sweepViewChoices() const;
    QString selectedSweepView() const { return selectedSweepView_; }
    QVariantList sweepMetricChoices() const;
    QString selectedSweepMetric() const { return selectedSweepMetric_; }
    QVariantList selectedSweepDistributionParamChoices() const;
    QString selectedSweepDistributionParam() const;
    QVariantList runs() const;
    int runCount() const noexcept { return static_cast<int>(records_.size()); }
    QString selectedRunId() const { return selectedRunId_; }
    QString selectedJson() const;
    QString selectedSummaryJson() const;
    QString selectedConfigText() const;
    QString selectedErrorText() const;
    QVariantList selectedEquityPoints() const;
    QVariantList resultScopeChoices() const;
    QString selectedResultScope() const;
    QVariantList selectedResultMetrics() const;
    QString selectedResultMetricKey() const;
    QString selectedResultMetricRatioKey() const { return selectedResultMetricRatioKey_; }
    QVariantList selectedResultMetricSeries() const;
    QVariantList resultMetricRatioChoices() const;
    QVariantList selectedSweepRows() const;
    QVariantList selectedSweepCurves() const;
    QVariantList selectedSweepDistributionBars() const;
    bool selectedIsSweep() const;
    bool hasEquityPoints() const;
    bool selectedPreviewLoading() const;
    bool selectedDetailsLoaded() const;
    bool selectedDetailsLoading() const;
    QString selectedDetailsErrorText() const { return selectedDetailsErrorText_; }
    qint64 selectedInitialBalanceE8() const;
    qint64 selectedPnlMinE8() const;
    qint64 selectedPnlMaxE8() const;
    bool hasSelection() const noexcept { return selectedRecord_() != nullptr; }
    QString statusText() const { return statusText_; }
    bool running() const noexcept { return running_; }
    bool canRun() const;
    int progressPercent() const noexcept { return progressPercent_; }
    QString progressText() const { return progressText_; }

    Q_INVOKABLE void reloadSessions();
    Q_INVOKABLE void setSelectedSessionId(const QString& sessionId);
    Q_INVOKABLE void setSessionPath(const QString& sessionPath);
    Q_INVOKABLE void setExtraSessionIds(const QString& sessionIds);
    Q_INVOKABLE void setSelectedSymbol(const QString& symbol);
    Q_INVOKABLE void setSelectedStrategy(const QString& strategy);
    Q_INVOKABLE void setConfigMode(const QString& mode);
    Q_INVOKABLE void setSelectedIndicatorProfile(const QString& profile);
    Q_INVOKABLE void setStrategyParameter(const QString& key, const QString& value);
    Q_INVOKABLE void setStrategyParameterGroup(int group, const QString& key);
    Q_INVOKABLE void setProfileName(const QString& profileName);
    Q_INVOKABLE void setPingLatencyUs(const QString& value);
    Q_INVOKABLE void setLatencySeed(const QString& value);
    Q_INVOKABLE void setMarketDataLatencyUs(const QString& value);
    Q_INVOKABLE void setMarketDataJitterUs(const QString& value);
    Q_INVOKABLE void setMarketOrderLatencyUs(const QString& value);
    Q_INVOKABLE void setMarketOrderJitterUs(const QString& value);
    Q_INVOKABLE void setLimitOrderLatencyUs(const QString& value);
    Q_INVOKABLE void setLimitOrderJitterUs(const QString& value);
    Q_INVOKABLE void setCancelOrderLatencyUs(const QString& value);
    Q_INVOKABLE void setCancelOrderJitterUs(const QString& value);
    Q_INVOKABLE void setUserDataLatencyUs(const QString& value);
    Q_INVOKABLE void setUserDataJitterUs(const QString& value);
    Q_INVOKABLE void setVenueExecutionValue(int legIndex, const QString& field, const QString& value);
    Q_INVOKABLE void setInitialBalanceUsdt(const QString& value);
    Q_INVOKABLE void setRiskEnabled(bool enabled);
    Q_INVOKABLE void setRiskMinEquityPct(const QString& value);
    Q_INVOKABLE void setRiskMinLegEquityPct(const QString& value);
    Q_INVOKABLE void setRiskMinLegEquityUsdt(const QString& value);
    Q_INVOKABLE void setRiskMaxPositionUsdt(const QString& value);
    Q_INVOKABLE void setRiskRateLimitGuardMinRemaining(const QString& value);
    Q_INVOKABLE void setRateLimitsEnabled(bool enabled);
    Q_INVOKABLE void setMakerFeeBps(const QString& value);
    Q_INVOKABLE void setTakerFeeBps(const QString& value);
    Q_INVOKABLE void setOrderLatencyUs(const QString& value);
    Q_INVOKABLE void setCancelLatencyUs(const QString& value);
    Q_INVOKABLE void setSweepBudget(const QString& value);
    Q_INVOKABLE void setSweepSeed(const QString& value);
    Q_INVOKABLE void setSelectedSweepCurveLimit(const QString& limit);
    Q_INVOKABLE void setSelectedSweepView(const QString& view);
    Q_INVOKABLE void setSelectedSweepMetric(const QString& metric);
    Q_INVOKABLE void setSelectedSweepDistributionParam(const QString& key);
    Q_INVOKABLE void setStrategyParameterMode(const QString& key, const QString& mode);
    Q_INVOKABLE void setStrategyParameterRange(const QString& key, const QString& minValue, const QString& maxValue, const QString& stepValue);
    Q_INVOKABLE void saveProfile();
    Q_INVOKABLE void loadProfile();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshResults() { refresh(); }
    Q_INVOKABLE void selectRun(const QString& runId);
    Q_INVOKABLE void loadSelectedRunDetails();
    Q_INVOKABLE void unloadSelectedRunDetails();
    Q_INVOKABLE void setSelectedResultScope(const QString& scope);
    Q_INVOKABLE void setSelectedResultMetricKey(const QString& key);
    Q_INVOKABLE void setSelectedResultMetricRatioKey(const QString& key);
    Q_INVOKABLE bool deleteSelectedRun();
    Q_INVOKABLE void startBacktest();
    Q_INVOKABLE void startSweep();
    Q_INVOKABLE void applySweepPoint(int rowIndex);
    Q_INVOKABLE void applySweepPointById(int pointId);
    Q_INVOKABLE void startDetailedRunFromSweepPoint(int rowIndex);
    Q_INVOKABLE void startDetailedRunFromSweepPointById(int pointId);
    Q_INVOKABLE void cancelBacktest();

    void applyWorkerProgress(int percent, const QString& text);
    bool workerCancelRequested() const noexcept { return cancelRequested_.load(std::memory_order_acquire); }

  signals:
    void sessionsChanged();
    void selectedSessionChanged();
    void multiSessionChanged();
    void symbolChanged();
    void selectedStrategyChanged();
    void configChanged();
    void indicatorProfileChanged();
    void accountingChanged();
    void rateLimitsChanged();
    void strategyParametersChanged();
    void profileChanged();
    void latencyChanged();
    void sweepConfigChanged();
    void runsChanged();
    void selectionChanged();
    void selectedResultMetricChanged();
    void selectedResultScopeChanged();
    void previewLoadingChanged();
    void statusTextChanged();
    void runningChanged();
    void canRunChanged();
    void progressChanged();
    void detailsLoadingChanged();

  private:
    enum class RecordLoadMode {
        MetadataOnly,
        Preview,
        Details,
    };

    struct RunRecord {
        QString runId{};
        QString displayName{};
        QString configText{};
        QString status{};
        QString strategy{};
        QString filePath{};
        QString fileName{};
        QString rawJson{};
        QString summaryJson{};
        QString errorText{};
        QString pnlText{};
        QString manifestPath{};
        QString equityPath{};
        QString sweepRowsPath{};
        QString sweepCurvesPath{};
        QVariantList equityPoints{};
        QVariantList resultScopes{};
        QVariantList resultMetrics{};
        QHash<QString, QVariantList> scopedEquityPoints{};
        QHash<QString, QVariantList> scopedResultMetrics{};
        QHash<QString, qint64> scopedInitialBalanceE8{};
        QHash<QString, qint64> scopedPnlMinE8{};
        QHash<QString, qint64> scopedPnlMaxE8{};
        QVariantList sweepRows{};
        QVariantList sweepCurves{};
        QStringList sweepParamKeys{};
        QString detailsErrorText{};
        qint64 initialBalanceE8{0};
        qint64 totalPnlE8{0};
        qint64 pnlMinE8{0};
        qint64 pnlMaxE8{0};
        qint64 modifiedMs{0};
        qint64 manifestModifiedMs{0};
        qint64 manifestSize{-1};
        qint64 equityModifiedMs{0};
        qint64 equitySize{-1};
        qint64 sweepRowsModifiedMs{0};
        qint64 sweepRowsSize{-1};
        qint64 sweepCurvesModifiedMs{0};
        qint64 sweepCurvesSize{-1};
        int errorCount{0};
        bool valid{false};
        bool sweep{false};
        bool detailsLoaded{false};
    };

    static RunRecord loadRecord_(const QString& filePath, RecordLoadMode mode);
    static QString normalizedPath_(const QString& path);
    static QString sessionIdFromPath_(const QString& path);
    static qint64 fileStampMs_(const QString& path, qint64* sizeOut = nullptr);
    static bool fileStampMatches_(const QString& path, qint64 modifiedMs, qint64 size);

    const RunRecord* selectedRecord_() const noexcept;
    const RunRecord* recordForPath_(const QString& filePath) const noexcept;
    RunRecord* mutableRecordForRunId_(const QString& runId) noexcept;
    void scheduleRefresh_();
    void updateWatcher_();
    void setStatusText_(const QString& statusText);
    void refreshSessionGateStatus_();
    void setRunning_(bool running);
    void setProgress_(int percent, const QString& text);
    void setPreviewLoading_(bool loading, const QString& runId = QString{});
    void setDetailsLoading_(bool loading, const QString& runId = QString{});
    void clearRecordDetails_(RunRecord& record);
    void ensureSelectedPreviewLoaded_();
    void applyLoadedPreview_(std::uint64_t generation, const QString& runId, const RunRecord& loaded);
    void applyLoadedDetails_(std::uint64_t generation, const QString& runId, const RunRecord& loaded);
    QVariantList loadSessions_() const;
    void stopWorker_();
    QString runId_() const;
    QString displayName_() const;
    QString configSummary_(const QHash<QString, QString>& overrides = {}) const;
    void loadStrategyDefaults_();
    void loadPersistentConfig_();
    void loadSavedParameterValues_();
    void savePersistentConfig_();
    QString profilePath_() const;
    QString writeRunConfig_(const QString& runId, const QHash<QString, QString>& overrides = {}, bool fixedOnly = false);
    QStringList selectedSessionPaths_() const;
    QStringList orderedSessionPathsForRun_() const;
    bool strategySupportsSelectedSessionCount_() const;
    bool ensureSelectedStrategySupportsSessionCount_();
    qint64 decimalE8Value_(const QString& value, qint64 fallback) const noexcept;
    quint64 latencyValue_(const QString& value, quint64 fallback) const noexcept;
    QString venueExecutionValue_(const QString& venueKey, const QString& field, const QString& fallback) const;
    QString venueExecutionOverrideValue_(const QString& venueKey, const QString& field) const;
    std::vector<QVariantMap> venueExecutionRows_() const;
    QString effectiveResultScopeId_(const RunRecord& record) const;
    void startBacktestWithOverrides_(const QHash<QString, QString>& overrides, const QString& suffix);

    QFileSystemWatcher watcher_{};
    QTimer refreshTimer_{};
    QSettings settings_{};
    QString selectedSessionId_{};
    QString manualSessionPath_{};
    QString extraSessionIds_{};
    QString symbolOverride_{};
    QString selectedRunId_{};
    QString activeRunId_{};
    QString selectedStrategy_{QStringLiteral("spread_maker1and2")};
    QString configMode_{QStringLiteral("fixed")};
    QString selectedIndicatorProfile_{};
    QString profileName_{QStringLiteral("default")};
    QString pingLatencyUs_{QStringLiteral("1000")};
    QString latencySeed_{QStringLiteral("0")};
    QString marketDataLatencyUs_{QStringLiteral("250")};
    QString marketDataJitterUs_{QStringLiteral("100")};
    QString marketOrderLatencyUs_{QStringLiteral("2500")};
    QString marketOrderJitterUs_{QStringLiteral("1000")};
    QString limitOrderLatencyUs_{QStringLiteral("1800")};
    QString limitOrderJitterUs_{QStringLiteral("700")};
    QString cancelOrderLatencyUs_{QStringLiteral("1800")};
    QString cancelOrderJitterUs_{QStringLiteral("700")};
    QString userDataLatencyUs_{QStringLiteral("0")};
    QString userDataJitterUs_{QStringLiteral("0")};
    QString initialBalanceUsdt_{QStringLiteral("1000")};
    bool riskEnabled_{false};
    QString riskMinEquityPct_{};
    QString riskMinLegEquityPct_{};
    QString riskMinLegEquityUsdt_{};
    QString riskMaxPositionUsdt_{};
    QString riskRateLimitGuardMinRemaining_{};
    bool rateLimitsEnabled_{true};
    QString makerFeeBps_{QStringLiteral("0")};
    QString takerFeeBps_{QStringLiteral("0")};
    QString sweepBudget_{QStringLiteral("64")};
    QString sweepSeed_{QStringLiteral("0")};
    QString selectedSweepCurveLimit_{QStringLiteral("32")};
    QString selectedSweepView_{QStringLiteral("curves")};
    QString selectedSweepMetric_{QStringLiteral("total_pnl_e8")};
    QString selectedSweepDistributionParam_{};
    QString selectedResultScope_{QStringLiteral("portfolio")};
    QString selectedResultMetricKey_{QStringLiteral("total_pnl_e8")};
    QString selectedResultMetricRatioKey_{};
    QString statusText_{QStringLiteral("Select a session and strategy")};
    QString progressText_{QStringLiteral("Idle")};
    QString selectedDetailsErrorText_{};
    QString previewLoadingRunId_{};
    QString detailsLoadingRunId_{};
    QString pendingDetailsRunId_{};
    std::uint64_t previewLoadGeneration_{0};
    std::uint64_t detailsLoadGeneration_{0};
    int progressPercent_{0};
    bool running_{false};
    bool previewLoading_{false};
    bool detailsLoading_{false};
    std::atomic<bool> cancelRequested_{false};
    std::thread worker_{};
    QVariantList sessions_{};
    std::vector<RunRecord> records_{};
    QHash<QString, QString> paramValues_{};
    QHash<QString, QString> venueExecutionValues_{};
    QHash<QString, QString> paramModes_{};
    QHash<QString, QString> paramMinValues_{};
    QHash<QString, QString> paramMaxValues_{};
    QHash<QString, QString> paramStepValues_{};
    QHash<int, QString> activeParamByGroup_{};
    QStringList paramOrder_{};
};

}  // namespace hftrec::gui
