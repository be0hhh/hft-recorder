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
    Q_PROPERTY(QString inputFile READ inputFile WRITE setInputFile NOTIFY inputFileChanged)
    Q_PROPERTY(QString outputRoot READ outputRoot WRITE setOutputRoot NOTIFY outputRootChanged)
    Q_PROPERTY(QVariantList pipelines READ pipelines CONSTANT)
    Q_PROPERTY(QString selectedPipelineId READ selectedPipelineId WRITE setSelectedPipelineId NOTIFY selectedPipelineChanged)
    Q_PROPERTY(QString selectedPipelineLabel READ selectedPipelineLabel NOTIFY selectedPipelineChanged)
    Q_PROPERTY(QString selectedPipelineSummary READ selectedPipelineSummary NOTIFY selectedPipelineChanged)
    Q_PROPERTY(bool selectedPipelineAvailable READ selectedPipelineAvailable NOTIFY selectedPipelineChanged)
    Q_PROPERTY(QString inferredStream READ inferredStream NOTIFY inputFileChanged)
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

    QString inputFile() const { return inputFile_; }
    QString outputRoot() const { return outputRoot_; }
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

    Q_INVOKABLE void setInputFile(const QString& path);
    Q_INVOKABLE void setInputFileUrl(const QUrl& url);
    Q_INVOKABLE void setOutputRoot(const QString& path);
    Q_INVOKABLE void setOutputRootUrl(const QUrl& url);
    Q_INVOKABLE void setSelectedPipelineId(const QString& pipelineId);
    Q_INVOKABLE void runCompression();

  signals:
    void inputFileChanged();
    void outputRootChanged();
    void selectedPipelineChanged();
    void canRunChanged();
    void resultChanged();

  private:
    QString inputFile_{};
    QString outputRoot_{};
    QString selectedPipelineId_{};
    QString statusText_{"Select one trades.jsonl, bookticker.jsonl, or depth.jsonl file"};
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
