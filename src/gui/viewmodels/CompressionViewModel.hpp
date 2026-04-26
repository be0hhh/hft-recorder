#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include "hft_compressor/result.hpp"

namespace hftrec::gui {

class CompressionViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString inputFile READ inputFile WRITE setInputFile NOTIFY inputFileChanged)
    Q_PROPERTY(QString outputRoot READ outputRoot WRITE setOutputRoot NOTIFY outputRootChanged)
    Q_PROPERTY(QString inferredStream READ inferredStream NOTIFY inputFileChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY resultChanged)
    Q_PROPERTY(QString outputFile READ outputFile NOTIFY resultChanged)
    Q_PROPERTY(QString metricsFile READ metricsFile NOTIFY resultChanged)
    Q_PROPERTY(QString ratioText READ ratioText NOTIFY resultChanged)
    Q_PROPERTY(QString encodeSpeedText READ encodeSpeedText NOTIFY resultChanged)
    Q_PROPERTY(QString decodeSpeedText READ decodeSpeedText NOTIFY resultChanged)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY resultChanged)
    Q_PROPERTY(QString timingText READ timingText NOTIFY resultChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY inputFileChanged)

  public:
    explicit CompressionViewModel(QObject* parent = nullptr);

    QString inputFile() const { return inputFile_; }
    QString outputRoot() const { return outputRoot_; }
    QString inferredStream() const;
    QString statusText() const { return statusText_; }
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
    Q_INVOKABLE void runCompression();

  signals:
    void inputFileChanged();
    void outputRootChanged();
    void resultChanged();

  private:
    QString inputFile_{};
    QString outputRoot_{};
    QString statusText_{"Select one trades.jsonl, bookticker.jsonl, or depth.jsonl file"};
    hft_compressor::CompressionResult lastResult_{};
};

}  // namespace hftrec::gui