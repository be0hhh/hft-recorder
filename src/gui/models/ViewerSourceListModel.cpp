#include "gui/models/ViewerSourceListModel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QVariantMap>

#include "gui/viewmodels/CaptureViewModel.hpp"

namespace hftrec::gui {

namespace {

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
}

QString displayExchange(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return QStringLiteral("Unknown Exchange");
    value.replace('_', ' ');
    value.replace('-', ' ');
    QStringList parts = value.split(' ', Qt::SkipEmptyParts);
    for (QString& part : parts) {
        if (part.size() <= 3) part = part.toUpper();
        else part = part.left(1).toUpper() + part.mid(1).toLower();
    }
    return parts.join(' ');
}

QString displayMarket(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return QStringLiteral("Unknown Market");
    value.replace('_', ' ');
    value.replace('-', ' ');
    QStringList parts = value.split(' ', Qt::SkipEmptyParts);
    for (QString& part : parts) {
        part = part.left(1).toUpper() + part.mid(1).toLower();
    }
    return parts.join(' ');
}

QString buildLiveLabel(const QString& exchange, const QString& market, const QString& symbol) {
    return QStringLiteral("LIVE | %1 | %2 | %3")
        .arg(displayExchange(exchange),
             displayMarket(market),
             symbol.trimmed().isEmpty() ? QStringLiteral("Unknown Symbol") : symbol.trimmed());
}

}  // namespace

ViewerSourceListModel::ViewerSourceListModel(QObject* parent)
    : QAbstractListModel(parent) {
    rebuildEntries_();
}

void ViewerSourceListModel::reload() {
    rebuildEntries_();
}

QString ViewerSourceListModel::sessionPath(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.sessionPath;
    }
    return {};
}

QString ViewerSourceListModel::sourceKind(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.sourceKind;
    }
    return {};
}

QString ViewerSourceListModel::groupAt(int index) const {
    if (index < 0 || index >= entries_.size()) return {};
    return entries_.at(index).group;
}

bool ViewerSourceListModel::hasSource(const QString& sourceId) const {
    return indexOfSource(sourceId) >= 0;
}

int ViewerSourceListModel::indexOfSource(const QString& sourceId) const {
    for (qsizetype i = 0; i < entries_.size(); ++i) {
        if (entries_.at(i).id == sourceId) return static_cast<int>(i);
    }
    return -1;
}

QString ViewerSourceListModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

QObject* ViewerSourceListModel::captureViewModel() const {
    return captureVm_;
}

void ViewerSourceListModel::setCaptureViewModel(QObject* captureViewModel) {
    auto* typedVm = qobject_cast<CaptureViewModel*>(captureViewModel);
    if (typedVm == captureVm_) return;
    captureVm_ = typedVm;
    reconnectCaptureVm_();
    rebuildEntries_();
    emit captureViewModelChanged();
}

int ViewerSourceListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant ViewerSourceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) return {};

    const auto& entry = entries_.at(index.row());
    switch (role) {
        case IdRole: return entry.id;
        case LabelRole: return entry.label;
        case GroupRole: return entry.group;
        case GroupTitleRole: return entry.groupTitle;
        case SourceKindRole: return entry.sourceKind;
        case SessionPathRole: return entry.sessionPath;
        case SymbolRole: return entry.symbol;
        case ExchangeRole: return entry.exchange;
        case MarketRole: return entry.market;
        case LiveAvailableRole: return entry.liveAvailable;
        default: return {};
    }
}

QHash<int, QByteArray> ViewerSourceListModel::roleNames() const {
    return {
        {IdRole, "id"},
        {LabelRole, "label"},
        {GroupRole, "group"},
        {GroupTitleRole, "groupTitle"},
        {SourceKindRole, "sourceKind"},
        {SessionPathRole, "sessionPath"},
        {SymbolRole, "symbol"},
        {ExchangeRole, "exchange"},
        {MarketRole, "market"},
        {LiveAvailableRole, "liveAvailable"},
    };
}

void ViewerSourceListModel::reconnectCaptureVm_() {
    if (captureSourcesConnection_) disconnect(captureSourcesConnection_);
    if (captureVm_ == nullptr) return;
    captureSourcesConnection_ =
        connect(captureVm_, &CaptureViewModel::activeLiveSourcesChanged, this, &ViewerSourceListModel::rebuildEntries_);
}

QVariantList ViewerSourceListModel::currentLiveSources_() const {
    return captureVm_ != nullptr ? captureVm_->activeLiveSources() : QVariantList{};
}

void ViewerSourceListModel::rebuildEntries_() {
    QList<Entry> nextEntries;

    const auto liveSources = currentLiveSources_();
    nextEntries.reserve(static_cast<qsizetype>(liveSources.size()));
    for (const auto& sourceValue : liveSources) {
        const QVariantMap source = sourceValue.toMap();
        Entry entry{};
        entry.id = source.value(QStringLiteral("id")).toString();
        entry.exchange = source.value(QStringLiteral("exchange")).toString();
        entry.market = source.value(QStringLiteral("market")).toString();
        entry.symbol = source.value(QStringLiteral("symbol")).toString();
        entry.sourceKind = QStringLiteral("live");
        entry.group = QStringLiteral("live");
        entry.groupTitle = QStringLiteral("Live");
        entry.liveAvailable = source.value(QStringLiteral("liveAvailable"), true).toBool();
        entry.label = source.value(QStringLiteral("label")).toString();
        if (entry.label.isEmpty()) entry.label = buildLiveLabel(entry.exchange, entry.market, entry.symbol);
        if (!entry.id.isEmpty()) nextEntries.push_back(std::move(entry));
    }

    QDir recordingsDir(recordingsRoot());
    if (recordingsDir.exists()) {
        const auto recordedEntries = recordingsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                             QDir::Time | QDir::Reversed);
        for (const auto& recordedId : recordedEntries) {
            Entry entry{};
            entry.id = QStringLiteral("recorded:%1").arg(recordedId);
            entry.label = recordedId;
            entry.group = QStringLiteral("recorded");
            entry.groupTitle = QStringLiteral("Recorded");
            entry.sourceKind = QStringLiteral("recorded");
            entry.sessionPath = recordingsDir.absoluteFilePath(recordedId);
            nextEntries.push_back(std::move(entry));
        }
    }

    beginResetModel();
    entries_ = std::move(nextEntries);
    endResetModel();
}

}  // namespace hftrec::gui
