#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <atomic>
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
    Q_PROPERTY(QString selectedSymbol READ selectedSymbol WRITE setSelectedSymbol NOTIFY symbolChanged)
    Q_PROPERTY(QString backtestsDirectory READ backtestsDirectory NOTIFY selectedSessionChanged)
    Q_PROPERTY(QVariantList strategyChoices READ strategyChoices CONSTANT)
    Q_PROPERTY(QString selectedStrategy READ selectedStrategy WRITE setSelectedStrategy NOTIFY selectedStrategyChanged)
    Q_PROPERTY(QString configMode READ configMode WRITE setConfigMode NOTIFY configChanged)
    Q_PROPERTY(QVariantList configModeChoices READ configModeChoices NOTIFY selectedStrategyChanged)
    Q_PROPERTY(QVariantList strategyParameters READ strategyParameters NOTIFY strategyParametersChanged)
    Q_PROPERTY(QString profileName READ profileName WRITE setProfileName NOTIFY profileChanged)
    Q_PROPERTY(QString pingLatencyUs READ pingLatencyUs WRITE setPingLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString initialBalanceUsdt READ initialBalanceUsdt WRITE setInitialBalanceUsdt NOTIFY accountingChanged)
    Q_PROPERTY(QString makerFeeBps READ makerFeeBps WRITE setMakerFeeBps NOTIFY accountingChanged)
    Q_PROPERTY(QString takerFeeBps READ takerFeeBps WRITE setTakerFeeBps NOTIFY accountingChanged)
    Q_PROPERTY(QString orderLatencyUs READ orderLatencyUs WRITE setOrderLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString amendLatencyUs READ amendLatencyUs WRITE setAmendLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QString cancelLatencyUs READ cancelLatencyUs WRITE setCancelLatencyUs NOTIFY latencyChanged)
    Q_PROPERTY(QVariantList runs READ runs NOTIFY runsChanged)
    Q_PROPERTY(int runCount READ runCount NOTIFY runsChanged)
    Q_PROPERTY(QString selectedRunId READ selectedRunId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedJson READ selectedJson NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSummaryJson READ selectedSummaryJson NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedErrorText READ selectedErrorText NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedEquityPoints READ selectedEquityPoints NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedResultMetrics READ selectedResultMetrics NOTIFY selectionChanged)
    Q_PROPERTY(bool hasEquityPoints READ hasEquityPoints NOTIFY selectionChanged)
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
    QString selectedSymbol() const;
    QString backtestsDirectory() const;
    QVariantList strategyChoices() const;
    QString selectedStrategy() const { return selectedStrategy_; }
    QString configMode() const { return configMode_; }
    QVariantList configModeChoices() const;
    QVariantList strategyParameters() const;
    QString profileName() const { return profileName_; }
    QString pingLatencyUs() const { return pingLatencyUs_; }
    QString initialBalanceUsdt() const { return initialBalanceUsdt_; }
    QString makerFeeBps() const { return makerFeeBps_; }
    QString takerFeeBps() const { return takerFeeBps_; }
    QString orderLatencyUs() const { return pingLatencyUs_; }
    QString amendLatencyUs() const { return pingLatencyUs_; }
    QString cancelLatencyUs() const { return pingLatencyUs_; }
    QVariantList runs() const;
    int runCount() const noexcept { return static_cast<int>(records_.size()); }
    QString selectedRunId() const { return selectedRunId_; }
    QString selectedJson() const;
    QString selectedSummaryJson() const;
    QString selectedErrorText() const;
    QVariantList selectedEquityPoints() const;
    QVariantList selectedResultMetrics() const;
    bool hasEquityPoints() const;
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
    Q_INVOKABLE void setSelectedSymbol(const QString& symbol);
    Q_INVOKABLE void setSelectedStrategy(const QString& strategy);
    Q_INVOKABLE void setConfigMode(const QString& mode);
    Q_INVOKABLE void setStrategyParameter(const QString& key, const QString& value);
    Q_INVOKABLE void setProfileName(const QString& profileName);
    Q_INVOKABLE void setPingLatencyUs(const QString& value);
    Q_INVOKABLE void setInitialBalanceUsdt(const QString& value);
    Q_INVOKABLE void setMakerFeeBps(const QString& value);
    Q_INVOKABLE void setTakerFeeBps(const QString& value);
    Q_INVOKABLE void setOrderLatencyUs(const QString& value);
    Q_INVOKABLE void setAmendLatencyUs(const QString& value);
    Q_INVOKABLE void setCancelLatencyUs(const QString& value);
    Q_INVOKABLE void saveProfile();
    Q_INVOKABLE void loadProfile();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshResults() { refresh(); }
    Q_INVOKABLE void selectRun(const QString& runId);
    Q_INVOKABLE void startBacktest();
    Q_INVOKABLE void cancelBacktest();

    void applyWorkerProgress(int percent, const QString& text);
    bool workerCancelRequested() const noexcept { return cancelRequested_.load(std::memory_order_acquire); }

  signals:
    void sessionsChanged();
    void selectedSessionChanged();
    void symbolChanged();
    void selectedStrategyChanged();
    void configChanged();
    void accountingChanged();
    void strategyParametersChanged();
    void profileChanged();
    void latencyChanged();
    void runsChanged();
    void selectionChanged();
    void statusTextChanged();
    void runningChanged();
    void canRunChanged();
    void progressChanged();

  private:
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
        QVariantList equityPoints{};
        QVariantList resultMetrics{};
        qint64 initialBalanceE8{0};
        qint64 pnlMinE8{0};
        qint64 pnlMaxE8{0};
        qint64 modifiedMs{0};
        int errorCount{0};
        bool valid{false};
    };

    static RunRecord loadRecord_(const QString& filePath);
    static QString normalizedPath_(const QString& path);
    static QString sessionIdFromPath_(const QString& path);

    const RunRecord* selectedRecord_() const noexcept;
    void updateWatcher_();
    void setStatusText_(const QString& statusText);
    void setRunning_(bool running);
    void setProgress_(int percent, const QString& text);
    void stopWorker_();
    QString runId_() const;
    QString displayName_() const;
    QString configSummary_() const;
    void loadStrategyDefaults_();
    void loadPersistentConfig_();
    void loadSavedParameterValues_();
    void savePersistentConfig_();
    QString profilePath_() const;
    QString writeRunConfig_(const QString& runId);
    qint64 decimalE8Value_(const QString& value, qint64 fallback) const noexcept;
    quint64 latencyValue_(const QString& value, quint64 fallback) const noexcept;

    QFileSystemWatcher watcher_{};
    QSettings settings_{};
    QString selectedSessionId_{};
    QString manualSessionPath_{};
    QString symbolOverride_{};
    QString selectedRunId_{};
    QString selectedStrategy_{QStringLiteral("spread_maker1and2")};
    QString configMode_{QStringLiteral("fixed")};
    QString profileName_{QStringLiteral("default")};
    QString pingLatencyUs_{QStringLiteral("1000")};
    QString initialBalanceUsdt_{QStringLiteral("1000")};
    QString makerFeeBps_{QStringLiteral("0")};
    QString takerFeeBps_{QStringLiteral("0")};
    QString statusText_{QStringLiteral("Select a session and strategy")};
    QString progressText_{QStringLiteral("Idle")};
    int progressPercent_{0};
    bool running_{false};
    std::atomic<bool> cancelRequested_{false};
    std::thread worker_{};
    std::vector<RunRecord> records_{};
    QHash<QString, QString> paramValues_{};
    QStringList paramOrder_{};
};

}  // namespace hftrec::gui
