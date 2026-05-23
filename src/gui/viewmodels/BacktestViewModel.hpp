#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
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
    Q_PROPERTY(QString backtestsDirectory READ backtestsDirectory NOTIFY selectedSessionChanged)
    Q_PROPERTY(QVariantList strategyChoices READ strategyChoices CONSTANT)
    Q_PROPERTY(QString selectedStrategy READ selectedStrategy WRITE setSelectedStrategy NOTIFY selectedStrategyChanged)
    Q_PROPERTY(QVariantList runs READ runs NOTIFY runsChanged)
    Q_PROPERTY(int runCount READ runCount NOTIFY runsChanged)
    Q_PROPERTY(QString selectedRunId READ selectedRunId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedJson READ selectedJson NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSummaryJson READ selectedSummaryJson NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedErrorText READ selectedErrorText NOTIFY selectionChanged)
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
    QString backtestsDirectory() const;
    QVariantList strategyChoices() const;
    QString selectedStrategy() const { return selectedStrategy_; }
    QVariantList runs() const;
    int runCount() const noexcept { return static_cast<int>(records_.size()); }
    QString selectedRunId() const { return selectedRunId_; }
    QString selectedJson() const;
    QString selectedSummaryJson() const;
    QString selectedErrorText() const;
    bool hasSelection() const noexcept { return selectedRecord_() != nullptr; }
    QString statusText() const { return statusText_; }
    bool running() const noexcept { return running_; }
    bool canRun() const;
    int progressPercent() const noexcept { return progressPercent_; }
    QString progressText() const { return progressText_; }

    Q_INVOKABLE void reloadSessions();
    Q_INVOKABLE void setSelectedSessionId(const QString& sessionId);
    Q_INVOKABLE void setSessionPath(const QString& sessionPath);
    Q_INVOKABLE void setSelectedStrategy(const QString& strategy);
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
    void selectedStrategyChanged();
    void runsChanged();
    void selectionChanged();
    void statusTextChanged();
    void runningChanged();
    void canRunChanged();
    void progressChanged();

  private:
    struct RunRecord {
        QString runId{};
        QString status{};
        QString strategy{};
        QString filePath{};
        QString fileName{};
        QString rawJson{};
        QString summaryJson{};
        QString errorText{};
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

    QFileSystemWatcher watcher_{};
    QString selectedSessionId_{};
    QString manualSessionPath_{};
    QString selectedRunId_{};
    QString selectedStrategy_{QStringLiteral("spread_maker1and2")};
    QString statusText_{QStringLiteral("Select a session and strategy")};
    QString progressText_{QStringLiteral("Idle")};
    int progressPercent_{0};
    bool running_{false};
    std::atomic<bool> cancelRequested_{false};
    std::thread worker_{};
    std::vector<RunRecord> records_{};
};

}  // namespace hftrec::gui
