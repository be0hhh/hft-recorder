#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include "hft_compressor/result.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace hft_compressor {
class MetricsServer;
}

namespace hftrec::gui {

class CompressionViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString recordingsRoot READ recordingsRoot CONSTANT)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QString selectedSessionId READ selectedSessionId WRITE setSelectedSessionId NOTIFY selectedSessionChanged)
    Q_PROPERTY(QString selectedSessionPath READ selectedSessionPath NOTIFY selectedSessionChanged)
    Q_PROPERTY(QVariantList channelChoices READ channelChoices NOTIFY channelChoicesChanged)
    Q_PROPERTY(QString selectedChannel READ selectedChannel WRITE setSelectedChannel NOTIFY selectedChannelChanged)
    Q_PROPERTY(QString inputFile READ inputFile NOTIFY selectionChanged)
    Q_PROPERTY(QString outputRoot READ outputRoot NOTIFY selectionChanged)
    Q_PROPERTY(QString outputFilePreview READ outputFilePreview NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList outputRootChoices READ outputRootChoices NOTIFY outputRootChoicesChanged)
    Q_PROPERTY(QVariantList pipelines READ pipelines NOTIFY selectedChannelChanged)
    Q_PROPERTY(QVariantList pipelineGroups READ pipelineGroups CONSTANT)
    Q_PROPERTY(QVariantList channelStats READ channelStats NOTIFY channelStatsChanged)
    Q_PROPERTY(QVariantList runRows READ runRows NOTIFY runRowsChanged)
    Q_PROPERTY(QVariantList verifyRows READ verifyRows NOTIFY verifyRowsChanged)
    Q_PROPERTY(QVariantList compressionBars READ compressionBars NOTIFY runRowsChanged)
    Q_PROPERTY(QVariantList decodeBars READ decodeBars NOTIFY verifyRowsChanged)
    Q_PROPERTY(QVariantList speedSeries READ speedSeries NOTIFY runRowsChanged)
    Q_PROPERTY(QVariantList decodeSpeedSeries READ decodeSpeedSeries NOTIFY verifyRowsChanged)
    Q_PROPERTY(QString emptyStateText READ emptyStateText NOTIFY sessionsChanged)
    Q_PROPERTY(QString metricsEndpointText READ metricsEndpointText CONSTANT)
    Q_PROPERTY(QString selectedPipelineId READ selectedPipelineId WRITE setSelectedPipelineId NOTIFY selectedPipelineChanged)
    Q_PROPERTY(QString selectedPipelineLabel READ selectedPipelineLabel NOTIFY selectedPipelineChanged)
    Q_PROPERTY(QString selectedPipelineSummary READ selectedPipelineSummary NOTIFY selectedPipelineChanged)
    Q_PROPERTY(bool selectedPipelineAvailable READ selectedPipelineAvailable NOTIFY selectedPipelineChanged)
    Q_PROPERTY(QString inferredStream READ inferredStream NOTIFY selectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY resultChanged)
    Q_PROPERTY(QString resultPipelineText READ resultPipelineText NOTIFY resultChanged)
    Q_PROPERTY(QString outputFile READ outputFile NOTIFY resultChanged)
    Q_PROPERTY(QString metricsFile READ metricsFile NOTIFY resultChanged)
    Q_PROPERTY(QString ratioText READ ratioText NOTIFY resultChanged)
    Q_PROPERTY(QString encodeSpeedText READ encodeSpeedText NOTIFY resultChanged)
    Q_PROPERTY(QString decodeSpeedText READ decodeSpeedText NOTIFY resultChanged)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY resultChanged)
    Q_PROPERTY(QString timingText READ timingText NOTIFY resultChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY canRunChanged)
    Q_PROPERTY(bool canDecodeVerify READ canDecodeVerify NOTIFY canDecodeVerifyChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool verifying READ verifying NOTIFY verifyingChanged)
    Q_PROPERTY(bool hasSessions READ hasSessions NOTIFY sessionsChanged)
    Q_PROPERTY(QString verifyStatusText READ verifyStatusText NOTIFY verifyResultChanged)
    Q_PROPERTY(QString verifyFile READ verifyFile NOTIFY verifyResultChanged)
    Q_PROPERTY(QString verifyCanonicalFile READ verifyCanonicalFile NOTIFY verifyResultChanged)
    Q_PROPERTY(QString verifySpeedText READ verifySpeedText NOTIFY verifyResultChanged)
    Q_PROPERTY(QString verifyExactText READ verifyExactText NOTIFY verifyResultChanged)
    Q_PROPERTY(QString verifyMismatchText READ verifyMismatchText NOTIFY verifyResultChanged)

  public:
    explicit CompressionViewModel(QObject* parent = nullptr);
    ~CompressionViewModel() override;

    QString recordingsRoot() const;
    QVariantList sessions() const;
    QString selectedSessionId() const { return selectedSessionId_; }
    QString selectedSessionPath() const;
    QVariantList channelChoices() const;
    QString selectedChannel() const { return selectedChannel_; }
    QString inputFile() const;
    QString outputRoot() const;
    QString outputFilePreview() const;
    QVariantList outputRootChoices() const;
    QVariantList pipelines() const;
    QVariantList pipelineGroups() const;
    QVariantList channelStats() const;
    QVariantList runRows() const;
    QVariantList verifyRows() const;
    QVariantList compressionBars() const;
    QVariantList decodeBars() const;
    QVariantList speedSeries() const;
    QVariantList decodeSpeedSeries() const;
    QString emptyStateText() const;
    QString metricsEndpointText() const;
    QString selectedPipelineId() const { return selectedPipelineId_; }
    QString selectedPipelineLabel() const;
    QString selectedPipelineSummary() const;
    bool selectedPipelineAvailable() const;
    QString inferredStream() const;
    QString statusText() const { return statusText_; }
    QString resultPipelineText() const { return resultPipelineText_; }
    QString outputFile() const;
    QString metricsFile() const;
    QString ratioText() const;
    QString encodeSpeedText() const;
    QString decodeSpeedText() const;
    QString sizeText() const;
    QString timingText() const;
    bool canRun() const;
    bool canDecodeVerify() const;
    bool running() const { return running_; }
    bool verifying() const { return verifying_; }
    bool hasSessions() const;
    QString verifyStatusText() const { return verifyStatusText_; }
    QString verifyFile() const { return verifyFile_; }
    QString verifyCanonicalFile() const { return verifyCanonicalFile_; }
    QString verifySpeedText() const;
    QString verifyExactText() const;
    QString verifyMismatchText() const;

    Q_INVOKABLE void reloadSessions();
    Q_INVOKABLE void setSelectedSessionId(const QString& sessionId);
    Q_INVOKABLE void setSelectedChannel(const QString& channel);
    Q_INVOKABLE void setInputFile(const QString& path);
    Q_INVOKABLE void setInputFileUrl(const QUrl& url);
    Q_INVOKABLE void setOutputRoot(const QString& path);
    Q_INVOKABLE void setOutputRootUrl(const QUrl& url);
    Q_INVOKABLE void setSelectedPipelineId(const QString& pipelineId);
    Q_INVOKABLE void runCompression();
    Q_INVOKABLE void runAllAvailableChannels();
    Q_INVOKABLE void decodeVerifySelected();
    Q_INVOKABLE void decodeVerifyAllAvailable();

  signals:
    void sessionsChanged();
    void selectedSessionChanged();
    void selectedChannelChanged();
    void channelChoicesChanged();
    void selectionChanged();
    void inputFileChanged();
    void outputRootChanged();
    void outputRootChoicesChanged();
    void selectedPipelineChanged();
    void canRunChanged();
    void canDecodeVerifyChanged();
    void resultChanged();
    void verifyResultChanged();
    void channelStatsChanged();
    void runRowsChanged();
    void verifyRowsChanged();
    void runningChanged();
    void verifyingChanged();

  private:
    QString existingChannelPath_(const QString& sessionPath, const QString& channel) const;
    QString preferredChannelPath_(const QString& sessionPath, const QString& channel) const;
    QString firstAvailableChannel_(const QString& sessionPath) const;
    QString channelFileName_(const QString& channel) const;
    void emitSelectionChanged_();
    void reloadStoredRunRows_();
    void reloadStoredVerifyRows_();
    void appendResultRow_(const QVariantMap& row);
    void appendVerifyRow_(const QVariantMap& row);
    void applyResult_(const hft_compressor::CompressionResult& result);
    void applyResults_(const std::vector<hft_compressor::CompressionResult>& results);
    void applyVerifyResult_(const hft_compressor::DecodeVerifyResult& result);
    void applyVerifyResults_(const std::vector<hft_compressor::DecodeVerifyResult>& results);
    QVariantMap resultRow_(const hft_compressor::CompressionResult& result) const;
    QVariantMap verifyRow_(const hft_compressor::DecodeVerifyResult& result) const;
    QVariantMap metricsRow_(const QString& metricsPath) const;
    QVariantMap verifyMetricsRow_(const QString& metricsPath) const;
    QVariantList rowsForSelectedChannel_(const QVariantList& rows) const;
    QString firstAvailablePipelineId_() const;
    QString outputFilePreviewFor_(const QString& channel) const;
    QString verifyFilePreviewFor_(const QString& channel) const;

    QString selectedSessionId_{};
    QString selectedChannel_{"trades"};
    QString selectedPipelineId_{};
    QString manualInputFile_{};
    QString manualOutputRoot_{};
    QString statusText_{"Жду выбор сессии и канала"};
    QString outputFile_{};
    QString metricsFile_{};
    QString resultPipelineText_{};
    QVariantList runRows_{};
    QVariantList verifyRows_{};
    bool running_{false};
    bool verifying_{false};
    QString verifyStatusText_{"Декодирование еще не запускалось"};
    QString verifyFile_{};
    QString verifyCanonicalFile_{};
    std::uint64_t verifyDecodedBytes_{0};
    std::uint64_t verifyCanonicalBytes_{0};
    std::uint64_t verifyMismatchOffset_{0};
    double verifyDecodeMbPerSec_{0.0};
    bool verifyExact_{false};
    std::uint64_t inputBytes_{0};
    std::uint64_t outputBytes_{0};
    std::uint64_t encodeNs_{0};
    std::uint64_t decodeNs_{0};
    std::uint64_t encodeCycles_{0};
    std::uint64_t decodeCycles_{0};
    double ratio_{0.0};
    double encodeMbPerSec_{0.0};
    double decodeMbPerSec_{0.0};
    std::unique_ptr<hft_compressor::MetricsServer> compressionMetricsServer_{};
};

}  // namespace hftrec::gui





