#include "gui/models/RecordingCatalog.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "core/recordings/RecordingRoot.hpp"

namespace hftrec::gui {
namespace {

constexpr int kCacheSchemaVersion = 2;

QString cleanPathText(const std::filesystem::path& path) {
    return QDir::cleanPath(QString::fromStdString(path.string()));
}

std::filesystem::path pathFromJson(const QJsonObject& object, const QString& key) {
    return std::filesystem::path{object.value(key).toString().toStdString()};
}

QJsonArray stringVectorToJson(const std::vector<std::string>& values) {
    QJsonArray out;
    for (const auto& value : values) out.push_back(QString::fromStdString(value));
    return out;
}

std::vector<std::string> stringVectorFromJson(const QJsonArray& values) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(values.size()));
    for (const auto& value : values) out.push_back(value.toString().toStdString());
    return out;
}

QJsonObject sessionToJson(const hftrec::recordings::RecordedSessionInfo& session) {
    QJsonObject out;
    out.insert(QStringLiteral("path"), cleanPathText(session.path));
    out.insert(QStringLiteral("manifest_path"), cleanPathText(session.manifestPath));
    out.insert(QStringLiteral("group_path"), cleanPathText(session.groupPath));
    out.insert(QStringLiteral("session_id"), QString::fromStdString(session.sessionId));
    out.insert(QStringLiteral("group_id"), QString::fromStdString(session.groupId));
    out.insert(QStringLiteral("group_title"), QString::fromStdString(session.groupTitle));
    out.insert(QStringLiteral("exchange"), QString::fromStdString(session.exchange));
    out.insert(QStringLiteral("market"), QString::fromStdString(session.market));
    out.insert(QStringLiteral("symbols"), stringVectorToJson(session.symbols));
    out.insert(QStringLiteral("normalized_symbol"), QString::fromStdString(session.normalizedSymbol));
    out.insert(QStringLiteral("session_health"), QString::fromStdString(session.sessionHealth));
    out.insert(QStringLiteral("warning_summary"), QString::fromStdString(session.warningSummary));
    out.insert(QStringLiteral("display_time"), QString::fromStdString(session.displayTime));
    out.insert(QStringLiteral("search_text"), QString::fromStdString(session.searchText));
    out.insert(QStringLiteral("started_at_ns"), static_cast<qint64>(session.startedAtNs));
    out.insert(QStringLiteral("ended_at_ns"), static_cast<qint64>(session.endedAtNs));
    out.insert(QStringLiteral("trade_count"), static_cast<qint64>(session.tradesCount));
    out.insert(QStringLiteral("bookticker_count"), static_cast<qint64>(session.bookTickerCount));
    out.insert(QStringLiteral("candle_count"), static_cast<qint64>(session.candleCount));
    out.insert(QStringLiteral("total_rows"), static_cast<qint64>(session.totalRows));
    out.insert(QStringLiteral("grouped"), session.grouped);
    out.insert(QStringLiteral("complete"), session.complete);
    return out;
}

hftrec::recordings::RecordedSessionInfo sessionFromJson(const QJsonObject& object) {
    hftrec::recordings::RecordedSessionInfo out;
    out.path = pathFromJson(object, QStringLiteral("path"));
    out.manifestPath = pathFromJson(object, QStringLiteral("manifest_path"));
    out.groupPath = pathFromJson(object, QStringLiteral("group_path"));
    out.sessionId = object.value(QStringLiteral("session_id")).toString().toStdString();
    out.groupId = object.value(QStringLiteral("group_id")).toString().toStdString();
    out.groupTitle = object.value(QStringLiteral("group_title")).toString().toStdString();
    out.exchange = object.value(QStringLiteral("exchange")).toString().toStdString();
    out.market = object.value(QStringLiteral("market")).toString().toStdString();
    out.symbols = stringVectorFromJson(object.value(QStringLiteral("symbols")).toArray());
    out.normalizedSymbol = object.value(QStringLiteral("normalized_symbol")).toString().toStdString();
    out.sessionHealth = object.value(QStringLiteral("session_health")).toString(QStringLiteral("clean")).toStdString();
    out.warningSummary = object.value(QStringLiteral("warning_summary")).toString().toStdString();
    out.displayTime = object.value(QStringLiteral("display_time")).toString().toStdString();
    out.searchText = object.value(QStringLiteral("search_text")).toString().toStdString();
    out.startedAtNs = object.value(QStringLiteral("started_at_ns")).toInteger();
    out.endedAtNs = object.value(QStringLiteral("ended_at_ns")).toInteger();
    out.tradesCount = static_cast<std::uint64_t>(object.value(QStringLiteral("trade_count")).toInteger());
    out.bookTickerCount = static_cast<std::uint64_t>(object.value(QStringLiteral("bookticker_count")).toInteger());
    out.candleCount = static_cast<std::uint64_t>(object.value(QStringLiteral("candle_count")).toInteger());
    out.totalRows = static_cast<std::uint64_t>(object.value(QStringLiteral("total_rows")).toInteger());
    out.grouped = object.value(QStringLiteral("grouped")).toBool();
    out.complete = object.value(QStringLiteral("complete")).toBool();
    return out;
}

QJsonObject groupToJson(const hftrec::recordings::RecordingGroupInfo& group) {
    QJsonObject out;
    out.insert(QStringLiteral("path"), cleanPathText(group.path));
    out.insert(QStringLiteral("id"), QString::fromStdString(group.id));
    out.insert(QStringLiteral("title"), QString::fromStdString(group.title));
    out.insert(QStringLiteral("normalized_symbol"), QString::fromStdString(group.normalizedSymbol));
    out.insert(QStringLiteral("display_time"), QString::fromStdString(group.displayTime));
    out.insert(QStringLiteral("search_text"), QString::fromStdString(group.searchText));
    out.insert(QStringLiteral("started_at_ns"), static_cast<qint64>(group.startedAtNs));
    out.insert(QStringLiteral("ended_at_ns"), static_cast<qint64>(group.endedAtNs));
    out.insert(QStringLiteral("total_rows"), static_cast<qint64>(group.totalRows));
    out.insert(QStringLiteral("physical"), group.physical);
    QJsonArray sessionIds;
    for (const auto& session : group.sessions) sessionIds.push_back(QString::fromStdString(session.sessionId));
    out.insert(QStringLiteral("session_ids"), sessionIds);
    return out;
}

hftrec::recordings::RecordingGroupInfo groupFromJson(
    const QJsonObject& object,
    const std::map<std::string, hftrec::recordings::RecordedSessionInfo>& sessionsById) {
    hftrec::recordings::RecordingGroupInfo out;
    out.path = pathFromJson(object, QStringLiteral("path"));
    out.id = object.value(QStringLiteral("id")).toString().toStdString();
    out.title = object.value(QStringLiteral("title")).toString().toStdString();
    out.normalizedSymbol = object.value(QStringLiteral("normalized_symbol")).toString().toStdString();
    out.displayTime = object.value(QStringLiteral("display_time")).toString().toStdString();
    out.searchText = object.value(QStringLiteral("search_text")).toString().toStdString();
    out.startedAtNs = object.value(QStringLiteral("started_at_ns")).toInteger();
    out.endedAtNs = object.value(QStringLiteral("ended_at_ns")).toInteger();
    out.totalRows = static_cast<std::uint64_t>(object.value(QStringLiteral("total_rows")).toInteger());
    out.physical = object.value(QStringLiteral("physical")).toBool();
    const QJsonArray sessionIds = object.value(QStringLiteral("session_ids")).toArray();
    out.sessions.reserve(static_cast<std::size_t>(sessionIds.size()));
    for (const auto& idValue : sessionIds) {
        const auto it = sessionsById.find(idValue.toString().toStdString());
        if (it != sessionsById.end()) out.sessions.push_back(it->second);
    }
    return out;
}

QJsonObject countsToJson(const BacktestLegCounts& counts) {
    QJsonObject out;
    out.insert(QStringLiteral("first"), counts.firstLeg);
    out.insert(QStringLiteral("second"), counts.secondLeg);
    out.insert(QStringLiteral("total"), counts.total);
    return out;
}

BacktestLegCounts countsFromJson(const QJsonObject& object) {
    BacktestLegCounts out;
    out.firstLeg = object.value(QStringLiteral("first")).toInt();
    out.secondLeg = object.value(QStringLiteral("second")).toInt();
    out.total = object.value(QStringLiteral("total")).toInt();
    return out;
}

QJsonDocument snapshotToJson(const RecordingCatalogSnapshot& snapshot) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("hftrec.recording_catalog.v2"));
    root.insert(QStringLiteral("schema_version"), kCacheSchemaVersion);
    root.insert(QStringLiteral("recordings_root"), snapshot.recordingsRoot);
    root.insert(QStringLiteral("status_text"), snapshot.statusText);

    QJsonArray sessions;
    for (const auto& session : snapshot.discovery.sessions) sessions.push_back(sessionToJson(session));
    root.insert(QStringLiteral("sessions"), sessions);

    QJsonArray groups;
    for (const auto& group : snapshot.discovery.groups) groups.push_back(groupToJson(group));
    root.insert(QStringLiteral("groups"), groups);

    QJsonObject counts;
    for (auto it = snapshot.backtestCountsBySession.constBegin(); it != snapshot.backtestCountsBySession.constEnd(); ++it) {
        counts.insert(it.key(), countsToJson(it.value()));
    }
    root.insert(QStringLiteral("backtest_counts"), counts);
    return QJsonDocument(root);
}

RecordingCatalogSnapshot snapshotFromJson(const QJsonDocument& doc, const QString& expectedRecordingsRoot, QString* errorText) {
    RecordingCatalogSnapshot out;
    if (!doc.isObject()) {
        if (errorText != nullptr) *errorText = QStringLiteral("recording catalog cache is not a JSON object");
        return out;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema_version")).toInt() != kCacheSchemaVersion) {
        if (errorText != nullptr) *errorText = QStringLiteral("recording catalog cache schema mismatch");
        return out;
    }
    out.recordingsRoot = QDir::cleanPath(root.value(QStringLiteral("recordings_root")).toString());
    if (out.recordingsRoot != QDir::cleanPath(expectedRecordingsRoot)) {
        if (errorText != nullptr) *errorText = QStringLiteral("recording catalog cache root mismatch");
        return {};
    }

    std::map<std::string, hftrec::recordings::RecordedSessionInfo> sessionsById;
    const QJsonArray sessions = root.value(QStringLiteral("sessions")).toArray();
    out.discovery.sessions.reserve(static_cast<std::size_t>(sessions.size()));
    for (const auto& sessionValue : sessions) {
        const auto session = sessionFromJson(sessionValue.toObject());
        if (session.sessionId.empty()) continue;
        out.discovery.sessions.push_back(session);
        sessionsById.emplace(session.sessionId, session);
    }

    const QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    out.discovery.groups.reserve(static_cast<std::size_t>(groups.size()));
    for (const auto& groupValue : groups) {
        auto group = groupFromJson(groupValue.toObject(), sessionsById);
        if (!group.id.empty()) out.discovery.groups.push_back(std::move(group));
    }

    const QJsonObject counts = root.value(QStringLiteral("backtest_counts")).toObject();
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        out.backtestCountsBySession.insert(it.key(), countsFromJson(it.value().toObject()));
    }
    out.loaded = true;
    out.fromDisk = true;
    out.statusText = QStringLiteral("Loaded recording catalog cache: %1 sessions").arg(out.discovery.sessions.size());
    return out;
}

}  // namespace

RecordingCatalogSnapshot buildRecordingCatalogSnapshot(const QString& recordingsRoot) {
    RecordingCatalogSnapshot out;
    out.recordingsRoot = QDir::cleanPath(recordingsRoot);
    out.discovery = hftrec::recordings::discoverRecordings(out.recordingsRoot.toStdString());
    out.backtestCountsBySession = backtestLegCountsBySession(out.discovery.sessions);
    out.loaded = true;
    out.fromDisk = false;
    int backtestRefs = 0;
    for (auto it = out.backtestCountsBySession.constBegin(); it != out.backtestCountsBySession.constEnd(); ++it) {
        backtestRefs += it.value().total > 0 ? it.value().total : it.value().firstLeg + it.value().secondLeg;
    }
    out.statusText = QStringLiteral("Indexed %1 sessions / %2 groups / %3 backtest refs")
        .arg(out.discovery.sessions.size())
        .arg(out.discovery.groups.size())
        .arg(backtestRefs);
    return out;
}

QString defaultRecordingCatalogCachePath() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.trimmed().isEmpty()) {
        const QString appName = QCoreApplication::applicationName().trimmed();
        base = QDir(QDir::tempPath()).absoluteFilePath(appName.isEmpty() ? QStringLiteral("hft-recorder") : appName);
    }
    QDir dir(base);
    (void)dir.mkpath(QStringLiteral("."));
    return dir.absoluteFilePath(QStringLiteral("recording_catalog_v2.json"));
}

bool writeRecordingCatalogCache(const QString& path, const RecordingCatalogSnapshot& snapshot, QString* errorText) {
    if (path.trimmed().isEmpty() || !snapshot.loaded) return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorText != nullptr) *errorText = file.errorString();
        return false;
    }
    const QByteArray bytes = snapshotToJson(snapshot).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size()) {
        if (errorText != nullptr) *errorText = file.errorString();
        return false;
    }
    if (!file.commit()) {
        if (errorText != nullptr) *errorText = file.errorString();
        return false;
    }
    return true;
}

RecordingCatalogSnapshot readRecordingCatalogCache(const QString& path, const QString& expectedRecordingsRoot, QString* errorText) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorText != nullptr) *errorText = file.errorString();
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return snapshotFromJson(doc, expectedRecordingsRoot, errorText);
}

RecordingCatalog::RecordingCatalog(QObject* parent)
    : QObject(parent),
      cachePath_(defaultRecordingCatalogCachePath()) {
    snapshot_.recordingsRoot = recordingsRoot();
    loadCache_();
    refreshAsync_(QStringLiteral("startup"));
}

RecordingCatalog::~RecordingCatalog() {
    if (asyncContext_) asyncContext_->alive.store(false, std::memory_order_release);
    stopWorkers_();
}

QString RecordingCatalog::recordingsRoot() const {
    return QDir::cleanPath(QString::fromStdString(hftrec::recordings::defaultRecordingsRoot().string()));
}

void RecordingCatalog::refresh() {
    refreshAsync_(QStringLiteral("manual"));
}

void RecordingCatalog::refreshAsync_(QString reason) {
    if (loading_) {
        pendingRefresh_ = true;
        return;
    }
    stopWorkers_();
    const std::uint64_t generation = ++loadGeneration_;
    setLoading_(true, QStringLiteral("Scanning recording catalog (%1)").arg(reason));
    const QString root = recordingsRoot();
    const QString cachePath = cachePath_;
    auto context = asyncContext_;
    RecordingCatalog* target = this;
    workers_.emplace_back([context, target, generation, root, cachePath]() {
        RecordingCatalogSnapshot loaded = buildRecordingCatalogSnapshot(root);
        QString cacheError;
        (void)writeRecordingCatalogCache(cachePath, loaded, &cacheError);
        if (!context || !context->alive.load(std::memory_order_acquire)) return;
        QMetaObject::invokeMethod(target, [context, target, generation, loaded = std::move(loaded)]() mutable {
            if (!context || !context->alive.load(std::memory_order_acquire)) return;
            target->applySnapshot_(generation, std::move(loaded));
        }, Qt::QueuedConnection);
    });
}

void RecordingCatalog::applySnapshot_(std::uint64_t generation, RecordingCatalogSnapshot snapshot) {
    if (generation != loadGeneration_) return;
    snapshot.revision = ++revisionCounter_;
    snapshot_ = std::move(snapshot);
    setLoading_(false, snapshot_.statusText);
    emit snapshotChanged();
    if (pendingRefresh_) {
        pendingRefresh_ = false;
        refreshAsync_(QStringLiteral("pending"));
    }
}

void RecordingCatalog::loadCache_() {
    QString error;
    RecordingCatalogSnapshot cached = readRecordingCatalogCache(cachePath_, recordingsRoot(), &error);
    if (!cached.loaded) {
        statusText_ = error.isEmpty() ? QStringLiteral("Recording catalog cache unavailable") : error;
        return;
    }
    cached.revision = ++revisionCounter_;
    snapshot_ = std::move(cached);
    statusText_ = snapshot_.statusText;
}

void RecordingCatalog::setLoading_(bool loading, const QString& statusText) {
    if (loading_ == loading && statusText_ == statusText) return;
    loading_ = loading;
    statusText_ = statusText;
    emit stateChanged();
}

void RecordingCatalog::stopWorkers_() noexcept {
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

}  // namespace hftrec::gui
