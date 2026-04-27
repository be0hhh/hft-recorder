#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <cstdint>
#include <memory>

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
    Q_PROPERTY(QVariantList pipelines READ pipelines CONSTANT)
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

    Q_INVOKABLE void reloadSessions();
    Q_INVOKABLE void setSelectedSessionId(const QString& sessionId);
    Q_INVOKABLE void setSelectedChannel(const QString& channel);
    Q_INVOKABLE void setInputFile(const QString& path);
    Q_INVOKABLE void setInputFileUrl(const QUrl& url);
    Q_INVOKABLE void setOutputRoot(const QString& path);
    Q_INVOKABLE void setOutputRootUrl(const QUrl& url);
    Q_INVOKABLE void setSelectedPipelineId(const QString& pipelineId);
    Q_INVOKABLE void runCompression();

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
    void resultChanged();

  private:
    QString existingChannelPath_(const QString& sessionPath, const QString& channel) const;
    QString preferredChannelPath_(const QString& sessionPath, const QString& channel) const;
    QString firstAvailableChannel_(const QString& sessionPath) const;
    QString channelFileName_(const QString& channel) const;
    void emitSelectionChanged_();

    QString selectedSessionId_{};
    QString selectedChannel_{"trades"};
    QString selectedPipelineId_{};
    QString manualInputFile_{};
    QString manualOutputRoot_{};
    QString statusText_{"Select a recording session and channel"};
    QString outputFile_{};
    QString metricsFile_{};
    QString resultPipelineText_{};
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