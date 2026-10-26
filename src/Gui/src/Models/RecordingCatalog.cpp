#include "RecordingCatalog.hpp"

#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <limits>
#include <set>
#include <string>
#include <utility>

#include "Corpus/Recordings/RecordingRoot.hpp"

namespace hftrec::gui {
namespace {

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
    out.insert(QStringLiteral("local_symbol"), QString::fromStdString(session.localSymbol));
    out.insert(QStringLiteral("folder_symbol"), QString::fromStdString(session.folderSymbol));
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
    out.insert(QStringLiteral("binary_corpus"),session.binaryCorpus);
    out.insert(QStringLiteral("source_count"),static_cast<qint64>(session.sourceCount));
    out.insert(QStringLiteral("channel_counts_available"),session.channelCountsAvailable);
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
    out.localSymbol = object.value(QStringLiteral("local_symbol")).toString().toStdString();
    out.folderSymbol = object.value(QStringLiteral("folder_symbol")).toString().toStdString();
    out.normalizedSymbol = object.value(QStringLiteral("normalized_symbol")).toString().toStdString();
    out.sessionHealth = object.value(QStringLiteral("session_health")).toString().toStdString();
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
    out.binaryCorpus=object.value(QStringLiteral("binary_corpus")).toBool();
    out.sourceCount=static_cast<std::uint32_t>(object.value(QStringLiteral("source_count")).toInteger());
    out.channelCountsAvailable=object.value(QStringLiteral("channel_counts_available")).toBool();
    return out;
}

QJsonObject groupToJson(const hftrec::recordings::RecordingGroupInfo& group) {
    QJsonObject out;
    out.insert(QStringLiteral("path"), cleanPathText(group.path));
    out.insert(QStringLiteral("id"), QString::fromStdString(group.id));
    out.insert(QStringLiteral("title"), QString::fromStdString(group.title));
    out.insert(QStringLiteral("local_symbol"), QString::fromStdString(group.localSymbol));
    out.insert(QStringLiteral("folder_symbol"), QString::fromStdString(group.folderSymbol));
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
    out.localSymbol = object.value(QStringLiteral("local_symbol")).toString().toStdString();
    out.folderSymbol = object.value(QStringLiteral("folder_symbol")).toString().toStdString();
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

bool nonnegativeInteger(const QJsonValue& value, qint64 maximum = std::numeric_limits<qint64>::max()) {
    if (!value.isDouble()) return false;
    const qint64 integer = value.toInteger(-1);
    return integer >= 0 && integer <= maximum && value.toDouble() == static_cast<double>(integer);
}

bool stringFields(const QJsonObject& object, std::initializer_list<const char*> fields) {
    for (const auto* field : fields) {
        if (!object.value(QLatin1String(field)).isString()) return false;
    }
    return true;
}

bool onlyFields(const QJsonObject& object,std::initializer_list<const char*> fields) {
    if(object.size()!=static_cast<qsizetype>(fields.size())) return false;
    for(const auto* field:fields) if(!object.contains(QLatin1String(field))) return false;
    return true;
}

bool sessionShape(const QJsonObject& object) {
    if(!onlyFields(object,{"path","manifest_path","group_path","session_id","group_id","group_title",
        "exchange","market","symbols","local_symbol","folder_symbol","normalized_symbol","session_health",
        "warning_summary","display_time","search_text","started_at_ns","ended_at_ns","trade_count",
        "bookticker_count","candle_count","total_rows","grouped","complete","binary_corpus","source_count",
        "channel_counts_available"})) return false;
    if(!object.value(QStringLiteral("binary_corpus")).isBool() ||
       !object.value(QStringLiteral("channel_counts_available")).isBool() ||
       !nonnegativeInteger(object.value(QStringLiteral("source_count")),std::numeric_limits<std::uint32_t>::max())) return false;
    if (!stringFields(object, {"path", "manifest_path", "group_path", "session_id", "group_id", "group_title",
            "exchange", "market", "local_symbol", "folder_symbol", "normalized_symbol", "session_health",
            "warning_summary", "display_time", "search_text"}) ||
        object.value(QStringLiteral("path")).toString().isEmpty() ||
        object.value(QStringLiteral("manifest_path")).toString().isEmpty() ||
        object.value(QStringLiteral("session_id")).toString().isEmpty() ||
        !object.value(QStringLiteral("grouped")).isBool() || !object.value(QStringLiteral("complete")).isBool() ||
        !object.value(QStringLiteral("symbols")).isArray() ||
        object.value(QStringLiteral("symbols")).toArray().size()>static_cast<qsizetype>(recordings::kRecordingMaxSymbols)) return false;
    for (const auto& symbol : object.value(QStringLiteral("symbols")).toArray()) {
        if (!symbol.isString()) return false;
    }
    for (const auto* field : {"started_at_ns", "ended_at_ns", "trade_count", "bookticker_count", "candle_count", "total_rows"}) {
        if (!nonnegativeInteger(object.value(QLatin1String(field)))) return false;
    }
    return true;
}

bool groupShape(const QJsonObject& object,
    const std::map<std::string, hftrec::recordings::RecordedSessionInfo>& sessionsById) {
    if(!onlyFields(object,{"path","id","title","local_symbol","folder_symbol","normalized_symbol","display_time",
        "search_text","started_at_ns","ended_at_ns","total_rows","physical","session_ids"})) return false;
    if (!stringFields(object, {"path", "id", "title", "local_symbol", "folder_symbol", "normalized_symbol",
            "display_time", "search_text"}) || object.value(QStringLiteral("id")).toString().isEmpty() ||
        !object.value(QStringLiteral("physical")).isBool() || !object.value(QStringLiteral("session_ids")).isArray() ||
        object.value(QStringLiteral("session_ids")).toArray().size()>static_cast<qsizetype>(recordings::kRecordingMaxSessions)) return false;
    for (const auto* field : {"started_at_ns", "ended_at_ns", "total_rows"}) {
        if (!nonnegativeInteger(object.value(QLatin1String(field)))) return false;
    }
    std::set<std::string> ids;
    for (const auto& value : object.value(QStringLiteral("session_ids")).toArray()) {
        if (!value.isString()) return false;
        const auto id = value.toString().toStdString();
        if (!sessionsById.contains(id) || !ids.insert(id).second) return false;
    }
    return true;
}

bool countsShape(const QJsonObject& object) {
    if(!onlyFields(object,{"first","second","total"})) return false;
    for (const auto* field : {"first", "second", "total"}) {
        if (!nonnegativeInteger(object.value(QLatin1String(field)), std::numeric_limits<int>::max())) return false;
    }
    const auto counts = countsFromJson(object);
    return counts.firstLeg <= std::numeric_limits<int>::max() - counts.secondLeg;
}

QJsonDocument snapshotToJson(const RecordingCatalogSnapshot& snapshot) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("hftrec.recording_catalog"));
    root.insert(QStringLiteral("recordings_root"), snapshot.recordingsRoot);
    root.insert(QStringLiteral("status_text"), snapshot.statusText);

    QJsonArray sessions;
    for (const auto& session : snapshot.discovery.sessions) sessions.push_back(sessionToJson(session));
    root.insert(QStringLiteral("sessions"), sessions);

    QJsonArray groups;
    for (const auto& group : snapshot.discovery.groups) groups.push_back(groupToJson(group));
    root.insert(QStringLiteral("groups"), groups);
    QJsonArray refreshRequired;
    for(const auto& path:snapshot.discovery.refreshRequired) refreshRequired.push_back(cleanPathText(path));
    root.insert(QStringLiteral("refresh_required"),refreshRequired);

    QJsonObject counts;
    for (auto it = snapshot.backtestCountsBySession.constBegin(); it != snapshot.backtestCountsBySession.constEnd(); ++it) {
        counts.insert(it.key(), countsToJson(it.value()));
    }
    root.insert(QStringLiteral("backtest_counts"), counts);
    return QJsonDocument(root);
}

RecordingCatalogSnapshot snapshotFromJson(const QJsonDocument& doc, const QString& expectedRecordingsRoot, QString* errorText) {
    if (errorText != nullptr) errorText->clear();
    const auto reject = [&](const QString& detail) {
        if (errorText != nullptr) *errorText = QStringLiteral("recording catalog cache refused: ") + detail;
        return RecordingCatalogSnapshot{};
    };
    if (!doc.isObject()) return reject(QStringLiteral("root is not a JSON object"));
    const QJsonObject root = doc.object();
    if(!onlyFields(root,{"schema","recordings_root","status_text","sessions","groups","backtest_counts","refresh_required"}))
        return reject(QStringLiteral("root fields differ from the current catalog shape"));
    if (!stringFields(root, {"schema", "recordings_root", "status_text"}) ||
        root.value(QStringLiteral("schema")).toString() != QStringLiteral("hftrec.recording_catalog") ||
        root.value(QStringLiteral("recordings_root")).toString().isEmpty() ||
        !root.value(QStringLiteral("sessions")).isArray() || !root.value(QStringLiteral("groups")).isArray() ||
        !root.value(QStringLiteral("backtest_counts")).isObject() ||
        !root.value(QStringLiteral("refresh_required")).isArray()) return reject(QStringLiteral("root fields have an invalid shape"));
    RecordingCatalogSnapshot out;
    out.recordingsRoot = QDir::cleanPath(root.value(QStringLiteral("recordings_root")).toString());
    if (out.recordingsRoot != QDir::cleanPath(expectedRecordingsRoot)) return reject(QStringLiteral("recordings root mismatch"));

    std::map<std::string, hftrec::recordings::RecordedSessionInfo> sessionsById;
    const QJsonArray sessions = root.value(QStringLiteral("sessions")).toArray();
    if(sessions.size()>static_cast<qsizetype>(recordings::kRecordingMaxSessions))
        return reject(QStringLiteral("session count exceeds the catalog bound"));
    out.discovery.sessions.reserve(static_cast<std::size_t>(sessions.size()));
    for (const auto& sessionValue : sessions) {
        if (!sessionValue.isObject() || !sessionShape(sessionValue.toObject())) return reject(QStringLiteral("session fields have an invalid shape"));
        auto session = sessionFromJson(sessionValue.toObject());
        if (!sessionsById.emplace(session.sessionId, session).second) return reject(QStringLiteral("duplicate session identity"));
        out.discovery.sessions.push_back(std::move(session));
    }

    std::set<std::string> groupIds;
    std::set<std::string> groupedSessionIds;
    const QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    if(groups.size()>static_cast<qsizetype>(recordings::kRecordingMaxGroups))
        return reject(QStringLiteral("group count exceeds the catalog bound"));
    out.discovery.groups.reserve(static_cast<std::size_t>(groups.size()));
    for (const auto& groupValue : groups) {
        if (!groupValue.isObject() || !groupShape(groupValue.toObject(), sessionsById)) return reject(QStringLiteral("group fields or session references have an invalid shape"));
        for(const auto& id:groupValue.toObject().value(QStringLiteral("session_ids")).toArray()) {
            if(!groupedSessionIds.insert(id.toString().toStdString()).second)
                return reject(QStringLiteral("session belongs to multiple cached groups"));
        }
        auto group = groupFromJson(groupValue.toObject(), sessionsById);
        if (!groupIds.insert(group.id).second) return reject(QStringLiteral("duplicate group identity"));
        out.discovery.groups.push_back(std::move(group));
    }

    const QJsonObject counts = root.value(QStringLiteral("backtest_counts")).toObject();
    if(counts.size()>static_cast<qsizetype>(recordings::kRecordingMaxSessions))
        return reject(QStringLiteral("backtest count map exceeds the catalog bound"));
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isObject() || !countsShape(it.value().toObject())) return reject(QStringLiteral("backtest counts must be integers in [0, INT_MAX]"));
        out.backtestCountsBySession.insert(it.key(), countsFromJson(it.value().toObject()));
    }
    const QJsonArray refreshRequired=root.value(QStringLiteral("refresh_required")).toArray();
    if(refreshRequired.size()>static_cast<qsizetype>(recordings::kRecordingMaxGroups))
        return reject(QStringLiteral("metadata refresh count exceeds the group bound"));
    for(const auto& value:refreshRequired) {
        if(!value.isString()) return reject(QStringLiteral("metadata refresh paths have an invalid shape"));
        out.discovery.refreshRequired.emplace_back(value.toString().toStdString());
    }
    out.discovery.complete=true;
    std::string validationError;
    if(!recordings::validateRecordingDiscovery(out.recordingsRoot.toStdString(),out.discovery,&validationError))
        return reject(QString::fromStdString(validationError));
    out.loaded = true;
    out.fromDisk = true;
    out.statusText = QStringLiteral("Loaded recording catalog cache: %1 sessions").arg(out.discovery.sessions.size());
    if(!out.discovery.refreshRequired.empty()) out.statusText+=QStringLiteral("; %1 groups need metadata refresh")
        .arg(out.discovery.refreshRequired.size());
    return out;
}

}  // namespace

RecordingCatalogSnapshot buildRecordingCatalogSnapshot(const QString& recordingsRoot) {
    RecordingCatalogSnapshot out;
    out.recordingsRoot = QDir::cleanPath(recordingsRoot);
    out.discovery = hftrec::recordings::discoverRecordings(out.recordingsRoot.toStdString());
    if(!out.discovery.complete || !out.discovery.errors.empty()) {
        QStringList errors;
        for(const auto& error:out.discovery.errors) errors.push_back(QString::fromStdString(error));
        out.statusText=QStringLiteral("Recording catalog scan refused: %1")
            .arg(errors.empty()?QStringLiteral("incomplete discovery"):errors.join(QStringLiteral(" | ")));
        return out;
    }
    QString backtestError;
    out.backtestCountsBySession = backtestLegCountsBySession(out.discovery.sessions,&backtestError,out.recordingsRoot);
    out.loaded = true;
    out.fromDisk = false;
    qint64 backtestRefs = 0;
    for (auto it = out.backtestCountsBySession.constBegin(); it != out.backtestCountsBySession.constEnd(); ++it) {
        backtestRefs += it.value().total > 0 ? it.value().total : static_cast<qint64>(it.value().firstLeg) + it.value().secondLeg;
    }
    out.statusText = QStringLiteral("Indexed %1 sessions / %2 groups / %3 backtest refs")
        .arg(out.discovery.sessions.size())
        .arg(out.discovery.groups.size())
        .arg(backtestRefs);
    if(!backtestError.isEmpty()) out.statusText+=QStringLiteral("; backtest index refused: ")+backtestError;
    if(!out.discovery.refreshRequired.empty()) out.statusText+=QStringLiteral("; %1 groups need metadata refresh")
        .arg(out.discovery.refreshRequired.size());
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
    return dir.absoluteFilePath(QStringLiteral("recording_catalog.json"));
}

bool writeRecordingCatalogCache(const QString& path, const RecordingCatalogSnapshot& snapshot, QString* errorText) {
    if (errorText != nullptr) errorText->clear();
    if (path.trimmed().isEmpty() || !snapshot.loaded || !snapshot.discovery.complete || !snapshot.discovery.errors.empty()) return false;
    std::string validationError;
    if(!recordings::validateRecordingDiscovery(snapshot.recordingsRoot.toStdString(),snapshot.discovery,&validationError)) {
        if(errorText) *errorText=QString::fromStdString(validationError);
        return false;
    }
    const QJsonDocument document = snapshotToJson(snapshot);
    if (!snapshotFromJson(document, snapshot.recordingsRoot, errorText).loaded) return false;
    if (QFileInfo::exists(path)) {
        QString existingError;
        if (!readRecordingCatalogCache(path, snapshot.recordingsRoot, &existingError).loaded) {
            if (errorText != nullptr) *errorText = existingError;
            return false;
        }
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorText != nullptr) *errorText = file.errorString();
        return false;
    }
    const QByteArray bytes = document.toJson(QJsonDocument::Compact);
    if(bytes.size()>static_cast<qsizetype>(recordings::kRecordingMaxCacheBytes)) {
        if(errorText) *errorText=QStringLiteral("recording catalog cache exceeds the byte bound");
        return false;
    }
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
    if (errorText != nullptr) errorText->clear();
    const QFileInfo selected(path);
    std::string text,error;
    const auto result=recordings::readRecordingMetadata(selected.absolutePath().toStdString(),
        selected.absoluteFilePath().toStdString(),recordings::kRecordingMaxCacheBytes,text,&error);
    if(result==recordings::RecordingMetadataReadResult::Missing) return {};
    if(result!=recordings::RecordingMetadataReadResult::Ready) {
        if(errorText) *errorText=QStringLiteral("recording catalog cache refused: ")+QString::fromStdString(error);
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorText != nullptr) *errorText = QStringLiteral("recording catalog cache refused: ") +
            parseError.errorString();
        return {};
    }
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
        if (loaded.loaded && !writeRecordingCatalogCache(cachePath, loaded, &cacheError) && !cacheError.isEmpty()) {
            loaded.statusText += QStringLiteral("; ") + cacheError;
        }
        if (!context || !context->alive.load(std::memory_order_acquire)) return;
        QMetaObject::invokeMethod(target, [context, target, generation, loaded = std::move(loaded)]() mutable {
            if (!context || !context->alive.load(std::memory_order_acquire)) return;
            target->applySnapshot_(generation, std::move(loaded));
        }, Qt::QueuedConnection);
    });
}

void RecordingCatalog::applySnapshot_(std::uint64_t generation, RecordingCatalogSnapshot snapshot) {
    if (generation != loadGeneration_) return;
    if(!snapshot.loaded) {
        const QString status=snapshot.statusText+(snapshot_.loaded?QStringLiteral("; showing the previous catalog"):QString{});
        setLoading_(false,status);
        if(pendingRefresh_) {pendingRefresh_=false;refreshAsync_(QStringLiteral("pending"));}
        return;
    }
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
