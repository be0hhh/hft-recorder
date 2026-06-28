#include "gui/models/SessionListModel.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "gui/backtests/BacktestSessionSummary.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"

namespace hftrec::gui {

namespace {

QString resolveRecordingsRoot() {
    return QDir::cleanPath(QString::fromStdString(recordings::defaultRecordingsRoot().string()));
}

QString sessionSummary(const hftrec::recordings::RecordedSessionInfo& session, const BacktestLegCounts& backtestCounts) {
    QString summary = sessionBacktestSummaryText(static_cast<int>(session.bookTickerCount), backtestCounts, session.startedAtNs);
    if (session.candleCount > 0) summary += QStringLiteral(" | C %1").arg(session.candleCount);
    return appendSessionHealthSummary(summary,
                                      QString::fromStdString(session.sessionHealth),
                                      QString::fromStdString(session.warningSummary));
}

QString groupSummary(const hftrec::recordings::RecordingGroupInfo& group) {
    return QStringLiteral("%1 venues | rows %2")
        .arg(group.sessions.size())
        .arg(QString::number(static_cast<qulonglong>(group.totalRows)));
}

QString sessionLabel(const hftrec::recordings::RecordedSessionInfo& session) {
    return QStringLiteral("%1/%2 %3")
        .arg(QString::fromStdString(session.exchange),
             QString::fromStdString(session.market),
             QString::fromStdString(session.symbols.empty() ? session.normalizedSymbol : session.symbols.front()));
}

}  // namespace

SessionListModel::SessionListModel(QObject* parent)
    : QAbstractListModel(parent) {
    reload();
}

void SessionListModel::reload() {
    beginResetModel();
    allSessions_.clear();
    sessions_.clear();

    const auto backtestCounts = backtestLegCountsBySession(recordingsRoot());
    const auto discovery = hftrec::recordings::discoverRecordings(recordingsRoot().toStdString());
    for (const auto& group : discovery.groups) {
        Entry groupEntry{};
        groupEntry.sessionId = QStringLiteral("group:%1").arg(QString::fromStdString(group.id));
        groupEntry.label = QString::fromStdString(group.title);
        groupEntry.summary = groupSummary(group);
        groupEntry.path = QString::fromStdString(group.path.string());
        groupEntry.searchText = QString::fromStdString(group.searchText).toLower();
        groupEntry.isGroup = true;
        allSessions_.push_back(groupEntry);

        for (const auto& session : group.sessions) {
            Entry entry{};
            entry.sessionId = QString::fromStdString(session.sessionId);
            entry.label = sessionLabel(session);
            entry.summary = sessionSummary(session, backtestCounts.value(entry.sessionId));
            entry.path = QString::fromStdString(session.path.string());
            entry.searchText = QString::fromStdString(session.searchText).toLower();
            entry.isGroup = false;
            entry.indent = 1;
            allSessions_.push_back(std::move(entry));
        }
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
