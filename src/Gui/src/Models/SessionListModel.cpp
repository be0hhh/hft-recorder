#include "SessionListModel.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "../Backtests/Sessions/BacktestSessionSummary.hpp"
#include "RecordingCatalog.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"

namespace hftrec::gui {

namespace {

QString resolveRecordingsRoot() {
    return QDir::cleanPath(QString::fromStdString(recordings::defaultRecordingsRoot().string()));
}

QString sessionSummary(const hftrec::recordings::RecordedSessionInfo& session, const BacktestLegCounts& backtestCounts) {
    return recordedSessionSummary(session,backtestCounts);
}

QString groupSummary(const hftrec::recordings::RecordingGroupInfo& group) {
    return QStringLiteral("%1 sessions | stored records %2")
        .arg(group.sessions.size())
        .arg(QString::number(static_cast<qulonglong>(group.totalRows)));
}

QString sessionLabel(const hftrec::recordings::RecordedSessionInfo& session) {
    return recordedSessionLabel(session);
}

}  // namespace

SessionListModel::SessionListModel(QObject* parent)
    : QAbstractListModel(parent) {
}

void SessionListModel::reload() {
    if (recordingCatalog_ != nullptr) recordingCatalog_->refresh();
    rebuildFromCatalog_();
}

QObject* SessionListModel::recordingCatalog() const {
    return recordingCatalog_;
}

void SessionListModel::setRecordingCatalog(QObject* recordingCatalog) {
    auto* typedCatalog = qobject_cast<RecordingCatalog*>(recordingCatalog);
    if (typedCatalog == recordingCatalog_) return;
    recordingCatalog_ = typedCatalog;
    reconnectRecordingCatalog_();
    rebuildFromCatalog_();
    emit recordingCatalogChanged();
}

void SessionListModel::reconnectRecordingCatalog_() {
    if (catalogSnapshotConnection_) disconnect(catalogSnapshotConnection_);
    if (recordingCatalog_ == nullptr) return;
    catalogSnapshotConnection_ =
        connect(recordingCatalog_, &RecordingCatalog::snapshotChanged, this, &SessionListModel::rebuildFromCatalog_);
}

void SessionListModel::rebuildFromCatalog_() {
    beginResetModel();
    allSessions_.clear();
    sessions_.clear();

    if (recordingCatalog_ != nullptr && recordingCatalog_->hasSnapshot()) {
        const auto& snapshot = recordingCatalog_->snapshot();
        const auto appendSession=[&](const recordings::RecordedSessionInfo& session,int indent) {
            Entry entry{};
            entry.sessionId=QString::fromStdString(session.sessionId);
            entry.label=sessionLabel(session);
            entry.summary=sessionSummary(session,snapshot.backtestCountsBySession.value(entry.sessionId));
            entry.path=QString::fromStdString(session.path.string());
            entry.searchText=QString::fromStdString(session.searchText).toLower();
            entry.indent=indent;
            entry.binaryCorpus=session.binaryCorpus;
            entry.sourceCount=session.sourceCount;
            entry.channelCountsAvailable=session.channelCountsAvailable;
            entry.totalRecords=session.totalRows;
            allSessions_.push_back(std::move(entry));
        };
        for (const auto& group : snapshot.discovery.groups) {
            Entry groupEntry{};
            groupEntry.sessionId = QStringLiteral("group:%1").arg(QString::fromStdString(group.id));
            groupEntry.label = QString::fromStdString(group.title);
            groupEntry.summary = groupSummary(group);
            groupEntry.path = QString::fromStdString(group.path.string());
            groupEntry.searchText = QString::fromStdString(group.searchText).toLower();
            groupEntry.isGroup = true;
            allSessions_.push_back(groupEntry);

            for (const auto& session : group.sessions) {
                appendSession(session,1);
            }
        }
        for(const auto& session:snapshot.discovery.sessions) if(!session.grouped) appendSession(session,0);
    }

    sessions_ = allSessions_;
    endResetModel();
    applyFilter_();
}

QString SessionListModel::sessionPath(const QString& sessionId) const {
    if (sessionId.trimmed().isEmpty()) return {};
    for (const auto& entry : allSessions_) {
        if (entry.sessionId == sessionId) return entry.path;
    }
    return QDir(recordingsRoot()).absoluteFilePath(sessionId);
}

QString SessionListModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

void SessionListModel::setSearchText(const QString& searchText) {
    const QString next = searchText.trimmed();
    if (searchText_ == next) return;
    searchText_ = next;
    applyFilter_();
    emit searchTextChanged();
}

int SessionListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : sessions_.size();
}

QVariant SessionListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= sessions_.size()) return {};
    const Entry& entry = sessions_.at(index.row());
    if (role == SessionIdRole) return entry.sessionId;
    if (role == SessionSummaryRole) return entry.summary;
    if (role == LabelRole) return entry.label;
    if (role == PathRole) return entry.path;
    if (role == SearchTextRole) return entry.searchText;
    if (role == IsGroupRole) return entry.isGroup;
    if (role == IndentRole) return entry.indent;
    if(entry.isGroup) return {};
    if(role==BinaryCorpusRole) return entry.binaryCorpus;
    if(role==SourceCountRole) return entry.sourceCount;
    if(role==ChannelCountsAvailableRole) return entry.channelCountsAvailable;
    if(role==TotalRecordsRole) return entry.totalRecords;
    return {};
}

QHash<int, QByteArray> SessionListModel::roleNames() const {
    return {
        {SessionIdRole, "sessionId"},
        {SessionSummaryRole, "sessionSummary"},
        {LabelRole, "label"},
        {PathRole, "path"},
        {SearchTextRole, "searchText"},
        {IsGroupRole, "isGroup"},
        {IndentRole, "indent"},
        {BinaryCorpusRole,"binaryCorpus"},
        {SourceCountRole,"sourceCount"},
        {ChannelCountsAvailableRole,"channelCountsAvailable"},
        {TotalRecordsRole,"totalRecords"},
    };
}

void SessionListModel::applyFilter_() {
    const QString needle = searchText_.trimmed().toLower();
    beginResetModel();
    sessions_.clear();
    if (needle.isEmpty()) {
        sessions_ = allSessions_;
    } else {
        for (const auto& entry : allSessions_) {
            const QString haystack = (entry.label + QLatin1Char(' ') + entry.sessionId + QLatin1Char(' ') +
                                      entry.summary + QLatin1Char(' ') + entry.path + QLatin1Char(' ') +
                                      entry.searchText).toLower();
            if (haystack.contains(needle)) sessions_.push_back(entry);
        }
    }
    endResetModel();
}

}  // namespace hftrec::gui
