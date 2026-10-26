#include "RecordingGroups.hpp"

#include "RecordingDiscovery.hpp"
#include "Common/JsonString.hpp"
#include "Common/MiniJsonParser.hpp"
#include "hftrec/CorpusContract/SessionManifest.hpp"
#include "hftrec/CorpusContract/CorpusCut.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <utility>

namespace hftrec::recordings {
namespace {
namespace fs = std::filesystem;
constexpr const char* kGroupFile = "recording_group.json";

// Every allocation belongs to this cold operation/result. Input documents,
// traversal counts, text lengths and retained result bytes have terminal caps.
struct Refusal : std::runtime_error { using std::runtime_error::runtime_error; };
[[noreturn]] void refuse(const std::string& reason) { throw Refusal(reason); }
void require(bool condition, const std::string& reason) { if (!condition) refuse(reason); }

struct Fd {
    int value{-1};
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : value(other.value) { other.value = -1; }
    Fd& operator=(Fd&& other) noexcept {
        if (value >= 0) ::close(value);
        value = other.value; other.value = -1; return *this;
    }
};

bool textOk(const std::string& value, std::size_t maximum = kRecordingMaxPathBytes) {
    return value.size() <= maximum && value.find('\0') == std::string::npos;
}
void checkPathText(const fs::path& path) {
    const auto text = path.generic_string();
    require(!text.empty() && textOk(text) && text.size() < kRecordingMaxPathBytes,
            "recording path exceeds bound or contains NUL");
    for (const auto& part : path) require(part != "..", "parent path component is forbidden");
}
fs::path absolutePath(const fs::path& path) {
    checkPathText(path);
    const auto result = fs::absolute(path).lexically_normal();
    checkPathText(result);
    return result;
}

Fd openAbsoluteDirectory(const fs::path& path) {
    require(path.is_absolute(), "absolute recording directory required");
    Fd current{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    require(current.value >= 0, "cannot open filesystem root");
    for (const auto& component : path.relative_path()) {
        const auto name = component.string();
        if (name.empty() || name == ".") continue;
        require(name != "..", "parent path component is forbidden");
        Fd next{::openat(current.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        require(next.value >= 0, "recording directory missing, inaccessible or symlink: " + path.string());
        current = std::move(next);
    }
    return current;
}

struct Root {
    fs::path path;
    Fd fd;
    explicit Root(const fs::path& input) : path(absolutePath(input)), fd(openAbsoluteDirectory(path)) {}
};
fs::path relativeUnder(const Root& root, const fs::path& path, bool allowRoot = false) {
    const auto absolute = absolutePath(path);
    const auto relative = absolute.lexically_relative(root.path);
    require(!relative.empty() && !relative.is_absolute(), "recording path is outside selected root");
    for (const auto& component : relative)
        require(component != "..", "recording path escapes selected root");
    require(allowRoot || relative != ".", "group/session directory cannot be selected root");
    return relative;
}
Fd openRelativeDirectory(const Root& root, const fs::path& relative, bool* missing = nullptr) {
    Fd current{::dup(root.fd.value)};
    require(current.value >= 0, "cannot duplicate recording root descriptor");
    for (const auto& component : relative) {
        const auto name = component.string();
        if (name.empty() || name == ".") continue;
        require(name != ".." && component != component.root_path(), "invalid relative recording directory");
        Fd next{::openat(current.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        if (next.value < 0 && errno == ENOENT && missing) { *missing = true; return Fd{}; }
        require(next.value >= 0, "recording directory missing, inaccessible or symlink");
        current = std::move(next);
    }
    return current;
}
void checkRootIdentity(const Root& root) {
    auto current = openAbsoluteDirectory(root.path);
    struct stat expected{}, actual{};
    require(::fstat(root.fd.value, &expected) == 0 && ::fstat(current.value, &actual) == 0 &&
            expected.st_dev == actual.st_dev && expected.st_ino == actual.st_ino,
            "selected recording root changed during operation");
}
void checkDirectoryIdentity(const Root& root, const fs::path& relative, int fd) {
    auto current = openRelativeDirectory(root, relative);
    struct stat expected{}, actual{};
    require(::fstat(fd, &expected) == 0 && ::fstat(current.value, &actual) == 0 &&
            expected.st_dev == actual.st_dev && expected.st_ino == actual.st_ino,
            "recording directory changed during operation");
    checkRootIdentity(root);
}

struct FileEvidence { fs::path relative; struct stat status{}; bool exists{true}; };
bool sameFile(const struct stat& left, const struct stat& right) {
    return S_ISREG(right.st_mode) && left.st_dev == right.st_dev && left.st_ino == right.st_ino &&
           left.st_size == right.st_size && left.st_mtim.tv_sec == right.st_mtim.tv_sec &&
           left.st_mtim.tv_nsec == right.st_mtim.tv_nsec && left.st_ctim.tv_sec == right.st_ctim.tv_sec &&
           left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
}
std::string readPinnedFile(const Root& root, const fs::path& relative,
                           std::size_t maximum, RecordingMetadataReadMode mode,
                           std::size_t& consumed, bool& missing,
                           std::vector<FileEvidence>* evidence = nullptr) {
    checkPathText(root.path / relative);
    auto directory = openRelativeDirectory(root, relative.parent_path(), &missing);
    if (missing) return {};
    Fd file{::openat(directory.value, relative.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
    if (file.value < 0 && errno == ENOENT) { missing = true; return {}; }
    struct stat before{};
    require(file.value >= 0 && ::fstat(file.value, &before) == 0 && S_ISREG(before.st_mode),
            "metadata must be an accessible regular file: " + relative.string());
    require(before.st_size >= 0 && (mode == RecordingMetadataReadMode::FirstNonemptyLine ||
            static_cast<std::uintmax_t>(before.st_size) <= maximum),
            "metadata file exceeds byte bound: " + relative.string());
    std::string document;
    if (mode == RecordingMetadataReadMode::Document) {
        const auto bytes = static_cast<std::size_t>(before.st_size);
        document.resize(bytes);
        while (consumed < bytes) {
            const auto count = ::read(file.value, document.data() + consumed, bytes - consumed);
            if (count < 0 && errno == EINTR) continue;
            require(count > 0, "metadata read failed or file changed");
            consumed += static_cast<std::size_t>(count);
        }
    } else {
        std::array<char, 4096> chunk{};
        bool found = false;
        while (!found && consumed < maximum) {
            const auto count = ::read(file.value, chunk.data(), std::min(chunk.size(), maximum - consumed));
            if (count < 0 && errno == EINTR) continue;
            require(count >= 0, "metadata prefix read failed");
            if (count == 0) break;
            consumed += static_cast<std::size_t>(count);
            for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
                const auto ch = chunk[index];
                if (ch == '\n') {
                    if (document.find_first_not_of(" \t\r") != std::string::npos) { found = true; break; }
                    document.clear();
                } else document.push_back(ch);
            }
        }
        require(found || static_cast<std::uintmax_t>(before.st_size) <= consumed,
                "metadata first nonempty line exceeds prefix byte bound");
        if (document.find_first_not_of(" \t\r") == std::string::npos) document.clear();
    }
    struct stat after{}, named{};
    require(::fstat(file.value, &after) == 0 &&
            ::fstatat(directory.value, relative.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
            before.st_dev == named.st_dev && before.st_ino == named.st_ino &&
            before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
            before.st_mtim.tv_nsec == after.st_mtim.tv_nsec && before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
            before.st_ctim.tv_nsec == after.st_ctim.tv_nsec,
            "metadata changed while reading");
    checkDirectoryIdentity(root, relative.parent_path(), directory.value);
    if (evidence) evidence->push_back({relative, after});
    return document;
}
std::string readDocument(const Root& root, const fs::path& relative,
                         std::size_t maximum, std::size_t& aggregate,
                         std::vector<FileEvidence>* evidence = nullptr) {
    std::size_t consumed = 0; bool missing = false;
    auto document = readPinnedFile(root, relative,
        std::min(maximum, kRecordingMaxAggregateInputBytes - aggregate),
        RecordingMetadataReadMode::Document, consumed, missing, evidence);
    require(!missing, "recording metadata is missing: " + relative.string());
    aggregate += consumed;
    return document;
}

RecordedSessionInfo readBinarySession(const Root& root, const fs::path& relative,
                                      std::size_t& aggregate, std::vector<FileEvidence>* evidence);
void sessionBounds(const RecordedSessionInfo& session, std::size_t& bytes);
struct Inventory {
    std::vector<fs::path> manifests, artifacts;
    std::map<fs::path, RecordedSessionInfo> binarySessions;
    std::size_t entries{0};
    std::size_t proofInputBytes{0};
    std::size_t retainedBytes{0};
};
void scanDirectory(const Root& root, int fd, const fs::path& relative,
                   std::size_t depth, Inventory& inventory, std::vector<FileEvidence>* evidence) {
    // dup() shares the directory stream offset and would make a second scan
    // appear empty. Opening dot creates an independent open description.
    Fd duplicate{::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    require(duplicate.value >= 0, "cannot enumerate recording directory");
    DIR* raw = ::fdopendir(duplicate.value);
    require(raw != nullptr, "cannot enumerate recording directory");
    duplicate.value = -1;
    struct CloseDir { DIR* value; ~CloseDir() { ::closedir(value); } } listing{raw};
    // Sorting one directory is bounded by the global entry budget.
    std::vector<std::string> names;
    for (;;) {
        errno = 0;
        const auto* entry = ::readdir(raw);
        if (!entry) { require(errno == 0, "recording directory enumeration failed"); break; }
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        require(++inventory.entries <= kRecordingMaxEntries, "recording entry count exceeds bound");
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    const auto present = [&](const char* name) { return std::binary_search(names.begin(), names.end(), name); };
    const bool jsonSession = present("manifest.json");
    const bool binarySession = present("manifest.bin") || present("manifest.pending") || present("cut.bin");
    require(!(jsonSession && binarySession), "capture directory has ambiguous JSON and binary metadata owners");
    if (binarySession) {
        require(!relative.empty(), "selected root cannot itself be a binary recording session");
        require(!present(kGroupFile), "binary capture directory cannot also hold a group manifest");
        require(inventory.manifests.size() < kRecordingMaxSessions, "recording session count exceeds bound");
        auto session = readBinarySession(root, relative, inventory.proofInputBytes, evidence);
        sessionBounds(session, inventory.retainedBytes);
        const auto manifest = session.manifestPath.lexically_relative(root.path);
        inventory.manifests.push_back(manifest);
        inventory.binarySessions.emplace(manifest, std::move(session));
        checkDirectoryIdentity(root, relative, fd);
        return;
    }
    if (jsonSession) {
        require(!relative.empty(), "selected root cannot itself be a recording session");
        capture::SessionManifest manifest;
        const auto document = readDocument(root, relative / "manifest.json",
                                            kRecordingMaxSessionManifestBytes, inventory.proofInputBytes);
        require(capture::parseManifestJson(document, manifest) == Status::Ok && manifest.structurallyLoadable,
                "candidate capture session manifest is not current and structurally loadable");
        require(!std::binary_search(names.begin(), names.end(), kGroupFile),
                "capture session directory cannot also hold a group manifest");
        require(inventory.manifests.size() < kRecordingMaxSessions, "recording session count exceeds bound");
        inventory.manifests.push_back(relative / "manifest.json");
        checkDirectoryIdentity(root, relative, fd);
        // A proved capture root owns its backtests, sweeps and other outputs.
        // Their manifest.json files are not additional recording sessions.
        return;
    }
    for (const auto& name : names) {
        const auto child = relative / name;
        checkPathText(root.path / child);
        struct stat status{};
        require(::fstatat(fd, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0,
                "recording entry became inaccessible");
        require(!S_ISLNK(status.st_mode), "symlink inside selected recording root: " + child.string());
        if (S_ISDIR(status.st_mode)) {
            require(depth < kRecordingMaxDepth, "recording directory depth exceeds bound");
            Fd next{::openat(fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
            struct stat opened{};
            require(next.value >= 0 && ::fstat(next.value, &opened) == 0 &&
                    opened.st_dev == status.st_dev && opened.st_ino == status.st_ino,
                    "recording directory changed while enumerating");
            scanDirectory(root, next.value, child, depth + 1, inventory, evidence);
        } else if (name == kGroupFile) {
            require(S_ISREG(status.st_mode), "recording metadata is not a regular file");
            require(inventory.artifacts.size() < kRecordingMaxGroups, "recording group count exceeds bound");
            inventory.artifacts.push_back(child);
        }
    }
    checkDirectoryIdentity(root, relative, fd);
}
Inventory inventoryFor(const Root& root, std::vector<FileEvidence>* evidence = nullptr) {
    Inventory result;
    scanDirectory(root, root.fd.value, {}, 0, result, evidence);
    std::sort(result.manifests.begin(), result.manifests.end());
    std::sort(result.artifacts.begin(), result.artifacts.end());
    return result;
}
void verifySnapshot(const Root& root, const Inventory& original, const std::vector<FileEvidence>& evidence) {
    const auto current = inventoryFor(root);
    require(current.manifests == original.manifests && current.artifacts == original.artifacts,
            "recording membership changed before metadata publication");
    for (const auto& item : evidence) {
        auto directory = openRelativeDirectory(root, item.relative.parent_path());
        struct stat currentFile{};
        const auto status = ::fstatat(directory.value, item.relative.filename().c_str(), &currentFile, AT_SYMLINK_NOFOLLOW);
        require(item.exists ? (status == 0 && sameFile(item.status, currentFile)) : (status < 0 && errno == ENOENT),
                "recording metadata changed before publication");
    }
    checkRootIdentity(root);
}

std::uint64_t sumRows(std::uint64_t left, std::uint64_t right) {
    require(right <= std::numeric_limits<std::uint64_t>::max() - left, "recording row count overflow");
    return left + right;
}
void timesOk(std::int64_t start, std::int64_t end) {
    require(start >= 0 && end >= 0 && (end == 0 || end >= start), "invalid recording time range");
}
void addBytes(std::size_t& count, std::size_t bytes) {
    require(bytes <= kRecordingMaxCacheBytes - count, "recording presentation exceeds byte bound");
    count += bytes;
}
void accountText(std::size_t& bytes, const std::string& text,
                 std::size_t maximum = kRecordingMaxPathBytes) {
    require(textOk(text, maximum), "recording text exceeds bound or contains NUL");
    addBytes(bytes, text.size());
}
void sessionBounds(const RecordedSessionInfo& session, std::size_t& bytes) {
    addBytes(bytes, sizeof(session));
    for (const auto* path : {&session.path, &session.manifestPath, &session.groupPath})
        accountText(bytes, path->generic_string());
    for (const auto* text : {&session.sessionId, &session.groupId, &session.groupTitle,
                            &session.exchange, &session.market, &session.sessionHealth,
                            &session.warningSummary, &session.displayTime, &session.searchText})
        accountText(bytes, *text);
    accountText(bytes, session.localSymbol, kRecordingMaxSymbolBytes);
    accountText(bytes, session.normalizedSymbol, kRecordingMaxSymbolBytes);
    accountText(bytes, session.folderSymbol, 3 * kRecordingMaxSymbolBytes);
    require(!session.sessionId.empty() && session.symbols.size() <= kRecordingMaxSymbols,
            "recording identity or symbol count is invalid");
    require(session.binaryCorpus
                ? (session.sourceCount > 0 && session.sourceCount <= kRecordingMaxSymbols &&
                   !session.channelCountsAvailable && session.tradesCount == 0 &&
                   session.bookTickerCount == 0 && session.candleCount == 0 && session.endedAtNs > 0)
                : (session.sourceCount == 0 && session.channelCountsAvailable),
            "recording format/count availability fields are inconsistent");
    addBytes(bytes, session.symbols.size() * sizeof(std::string));
    for (const auto& symbol : session.symbols) {
        require(!symbol.empty(), "empty recording symbol");
        accountText(bytes, symbol, kRecordingMaxSymbolBytes);
    }
    timesOk(session.startedAtNs, session.endedAtNs);
    require(session.complete == (session.endedAtNs > 0), "recording finalized flag differs from its stored end time");
}
void groupBounds(const RecordingGroupInfo& group, std::size_t& bytes) {
    addBytes(bytes, sizeof(group));
    accountText(bytes, group.path.generic_string());
    for (const auto* text : {&group.id, &group.title, &group.displayTime, &group.searchText})
        accountText(bytes, *text);
    accountText(bytes, group.localSymbol, kRecordingMaxSymbolBytes);
    accountText(bytes, group.normalizedSymbol, kRecordingMaxSymbolBytes);
    accountText(bytes, group.folderSymbol, 3 * kRecordingMaxSymbolBytes);
    timesOk(group.startedAtNs, group.endedAtNs);
    require(group.sessions.size() <= kRecordingMaxSessions, "group membership exceeds bound");
}

RecordedSessionInfo readSession(const Root& root, const fs::path& relative, std::size_t& aggregate,
                                std::vector<FileEvidence>* evidence) {
    const auto document = readDocument(root, relative, kRecordingMaxSessionManifestBytes, aggregate, evidence);
    capture::SessionManifest manifest;
    require(capture::parseManifestJson(document, manifest) == Status::Ok && manifest.structurallyLoadable,
            "session manifest is not current and structurally loadable: " + relative.string());
    require(textOk(manifest.storageSymbol, kRecordingMaxSymbolBytes) &&
            textOk(manifest.selectedParentDir) && textOk(manifest.sessionStatus) &&
            manifest.symbols.size() <= kRecordingMaxSymbols, "session manifest text/symbol count exceeds bound");
    RecordedSessionInfo session;
    session.path = root.path / relative.parent_path();
    session.manifestPath = root.path / relative;
    session.sessionId = manifest.sessionId;
    session.exchange = manifest.exchange; session.market = manifest.market;
    session.symbols = manifest.symbols;
    const auto symbol = manifest.storageSymbol.empty()
        ? (manifest.symbols.empty() ? std::string{} : manifest.symbols.front()) : manifest.storageSymbol;
    session.localSymbol = recordingLocalSymbol(manifest.exchange, manifest.market, symbol);
    session.folderSymbol = recordingFolderSymbol(manifest.exchange, manifest.market, symbol);
    session.normalizedSymbol = session.localSymbol;
    session.sessionHealth = std::string(toString(manifest.sessionHealth));
    session.warningSummary = manifest.warningSummary;
    session.startedAtNs = manifest.startedAtNs; session.endedAtNs = manifest.endedAtNs;
    session.tradesCount = manifest.tradesCount;
    session.bookTickerCount = manifest.bookTickerCount;
    session.candleCount = manifest.candles2Count != 0 ? manifest.candles2Count : manifest.candlesCount;
    for (const auto rows : {manifest.tradesCount, manifest.liquidationsCount, manifest.bookTickerCount,
                           manifest.depthCount, manifest.candlesCount, manifest.candles2Count,
                           manifest.markPriceCount, manifest.indexPriceCount, manifest.fundingCount,
                           manifest.priceLimitCount}) session.totalRows = sumRows(session.totalRows, rows);
    session.complete = manifest.endedAtNs > 0;
    session.displayTime = recordingDisplayTimestamp(session.startedAtNs);
    session.searchText = session.sessionId + " " + session.exchange + " " + session.market;
    session.searchText += " " + session.localSymbol;
    std::size_t bytes = 0;
    sessionBounds(session, bytes);
    return session;
}

RecordedSessionInfo readBinarySession(const Root& root, const fs::path& relative,
                                      std::size_t& aggregate, std::vector<FileEvidence>* evidence) {
    auto directory = openRelativeDirectory(root, relative);
    constexpr std::array<const char*, 3> names{{"manifest.bin", "sources.bin", "cut.bin"}};
    std::array<FileEvidence, 3> files{};
    for (std::size_t index = 0; index < names.size(); ++index) {
        auto& file = files[index]; file.relative = relative / names[index];
        checkPathText(root.path / file.relative);
        const auto result = ::fstatat(directory.value, names[index], &file.status, AT_SYMLINK_NOFOLLOW);
        file.exists = result == 0;
        require(file.exists || errno == ENOENT, "binary recording metadata is inaccessible");
        require(!file.exists || S_ISREG(file.status.st_mode), "binary recording metadata is not a regular file");
    }
    corpus::CorpusRecordingMetadata metadata;
    std::uint64_t consumed = 0;
    std::string error;
    const auto remaining = kRecordingMaxAggregateInputBytes - aggregate;
    const auto status = corpus::readCorpusRecordingMetadataAt(directory.value, metadata, consumed, error, remaining);
    require(consumed <= remaining, "binary recording metadata exceeds aggregate input bound");
    aggregate += static_cast<std::size_t>(consumed);
    require(status == Status::Ok, error.empty() ? "binary recording is not ready or has invalid metadata" : error);
    require(metadata.finalized && metadata.manifest.finalizedReceiveNs > 0,
            "binary recording has no authoritative finalized metadata; not ready");
    for (std::size_t index = 0; index < names.size(); ++index) {
        struct stat current{};
        const auto result = ::fstatat(directory.value, names[index], &current, AT_SYMLINK_NOFOLLOW);
        const auto& file = files[index];
        require(file.exists ? (result == 0 && sameFile(file.status, current)) : (result < 0 && errno == ENOENT),
                "binary recording metadata changed while deriving catalog row");
        if (evidence) evidence->push_back(file);
    }
    checkDirectoryIdentity(root, relative, directory.value);
    require(!metadata.sources.empty() && metadata.sources.size() <= kRecordingMaxSymbols &&
            metadata.sources.size() == metadata.manifest.sourceCount,
            "binary recording source count exceeds catalog bound or differs from manifest");
    RecordedSessionInfo session;
    session.path = root.path / relative;
    session.manifestPath = session.path / "manifest.bin";
    session.sessionId = corpus::corpusIdentity(metadata.manifest) + ":" + relative.generic_string();
    session.startedAtNs = metadata.manifest.startedReceiveNs;
    session.endedAtNs = metadata.manifest.finalizedReceiveNs;
    session.totalRows = metadata.manifest.recordCount;
    session.sourceCount = metadata.manifest.sourceCount;
    session.binaryCorpus = true;
    session.channelCountsAvailable = false;
    session.complete = session.endedAtNs > 0;
    session.sessionHealth = metadata.manifest.gapCount == 0 ? "unknown" : "degraded";
    session.warningSummary = "Per-channel counts unavailable; total includes all typed records";
    if (metadata.manifest.gapCount != 0) session.warningSummary += "; recorded gaps present";
    std::set<std::string> symbols;
    bool first = true, sameExchange = true, sameMarket = true;
    for (const auto& source : metadata.sources) {
        const std::string exchange(source.venue.data(), source.venueBytes);
        const std::string market(source.market.data(), source.marketBytes);
        const std::string symbol(source.canonicalSymbol.data(), source.canonicalSymbolBytes);
        require(textOk(exchange) && textOk(market) && !symbol.empty() && textOk(symbol, kRecordingMaxSymbolBytes),
                "binary recording source text exceeds catalog bound");
        if (first) { session.exchange = exchange; session.market = market; first = false; }
        else { sameExchange = sameExchange && session.exchange == exchange; sameMarket = sameMarket && session.market == market; }
        symbols.insert(symbol);
    }
    if (!sameExchange) session.exchange.clear();
    if (!sameMarket) session.market.clear();
    session.symbols.assign(symbols.begin(), symbols.end());
    if (session.symbols.size() == 1 && !session.exchange.empty() && !session.market.empty()) {
        session.localSymbol = recordingLocalSymbol(session.exchange, session.market, session.symbols.front());
        session.folderSymbol = recordingFolderSymbol(session.exchange, session.market, session.symbols.front());
        session.normalizedSymbol = session.localSymbol;
    }
    session.displayTime = recordingDisplayTimestamp(session.startedAtNs);
    session.searchText = session.sessionId + " " + session.exchange + " " + session.market + " " + session.localSymbol;
    std::size_t retained = 0; sessionBounds(session, retained);
    return session;
}

RecordingGroupInfo parseArtifact(const std::string& document, std::vector<std::string>& ids) {
    json::MiniJsonParser parser(document);
    require(parser.parseObjectStart(), "group manifest must be a JSON object");
    RecordingGroupInfo group;
    std::set<std::string> seen;
    if (!parser.peek('}')) do {
        std::string key;
        require(parser.parseKey(key) && seen.insert(key).second, "duplicate or malformed group manifest field");
        bool ok = false;
        if (key == "schema") { std::string value; ok = parser.parseString(value) && value == "hftrec.recording_group"; }
        else if (key == "path") { std::string value; ok = parser.parseString(value); group.path = value; }
        else if (key == "id") ok = parser.parseString(group.id);
        else if (key == "title") ok = parser.parseString(group.title);
        else if (key == "local_symbol") ok = parser.parseString(group.localSymbol);
        else if (key == "folder_symbol") ok = parser.parseString(group.folderSymbol);
        else if (key == "normalized_symbol") ok = parser.parseString(group.normalizedSymbol);
        else if (key == "display_time") ok = parser.parseString(group.displayTime);
        else if (key == "search_text") ok = parser.parseString(group.searchText);
        else if (key == "started_at_ns") ok = parser.parseInt64(group.startedAtNs);
        else if (key == "ended_at_ns") ok = parser.parseInt64(group.endedAtNs);
        else if (key == "total_rows") ok = parser.parseUInt64(group.totalRows);
        else if (key == "physical") ok = parser.parseBool(group.physical);
        else if (key == "session_ids") {
            ok = parser.parseArrayStart();
            if (ok && !parser.peek(']')) do {
                std::string id;
                ok = ids.size() < kRecordingMaxSessions && parser.parseString(id) &&
                     !id.empty() && textOk(id);
                if (!ok) break;
                ids.push_back(std::move(id));
            } while (parser.parseComma());
            ok = ok && parser.parseArrayEnd();
        }
        require(ok, "unknown, incompatible or malformed group manifest field");
    } while (parser.parseComma());
    require(parser.parseObjectEnd() && parser.finish() && seen.size() == 14,
            "group manifest has missing fields or trailing input");
    require(group.physical && !ids.empty(), "group manifest must describe a nonempty physical group");
    std::set<std::string> unique(ids.begin(), ids.end());
    require(unique.size() == ids.size(), "group manifest has duplicate session IDs");
    std::size_t bytes = 0; groupBounds(group, bytes);
    return group;
}

std::vector<std::string> memberIds(const RecordingGroupInfo& group) {
    std::vector<std::string> ids;
    for (const auto& session : group.sessions) ids.push_back(session.sessionId);
    std::sort(ids.begin(), ids.end());
    return ids;
}
bool artifactMatches(const Root& root, const fs::path& relative,
                     const RecordingGroupInfo& parsed, const std::vector<std::string>& ids,
                     const RecordingGroupInfo& actual) {
    require(parsed.path.is_absolute() && parsed.path == root.path / relative &&
            parsed.path == actual.path && parsed.id == relative.generic_string() &&
            parsed.id == actual.id, "group manifest path/root/identity mismatch");
    auto sorted = ids; std::sort(sorted.begin(), sorted.end());
    const auto currentIds = memberIds(actual);
    require(std::includes(currentIds.begin(), currentIds.end(), sorted.begin(), sorted.end()),
            "group manifest references removed or foreign direct-child sessions");
    return sorted == currentIds;
}
void applyPresentation(RecordingGroupInfo& group, const RecordingGroupInfo& artifact) {
    group.title = artifact.title; group.localSymbol = artifact.localSymbol;
    group.folderSymbol = artifact.folderSymbol; group.normalizedSymbol = artifact.normalizedSymbol;
    group.displayTime = artifact.displayTime; group.searchText = artifact.searchText;
}

void summarize(RecordingGroupInfo& group) {
    require(!group.sessions.empty(), "empty physical recording group");
    group.startedAtNs = group.sessions.front().startedAtNs;
    group.localSymbol = group.sessions.front().localSymbol;
    group.folderSymbol = group.sessions.front().folderSymbol;
    group.normalizedSymbol = group.sessions.front().normalizedSymbol;
    for (const auto& session : group.sessions) {
        group.startedAtNs = std::min(group.startedAtNs, session.startedAtNs);
        group.endedAtNs = std::max(group.endedAtNs, session.endedAtNs);
        group.totalRows = sumRows(group.totalRows, session.totalRows);
        if (group.localSymbol != session.localSymbol) group.localSymbol.clear();
        if (group.folderSymbol != session.folderSymbol) group.folderSymbol.clear();
        if (group.normalizedSymbol != session.normalizedSymbol) group.normalizedSymbol.clear();
    }
    group.displayTime = recordingDisplayTimestamp(group.startedAtNs);
    group.searchText = group.title + " " + group.id + " " + group.normalizedSymbol;
}

RecordingDiscoveryResult discover(const Root& root, Inventory* snapshot = nullptr,
                                   std::vector<FileEvidence>* evidence = nullptr) {
    const auto inventory = inventoryFor(root, evidence);
    RecordingDiscoveryResult result;
    std::map<fs::path, std::size_t> groupIndexes;
    std::set<std::string> identities;
    std::set<fs::path> sessionDirectories;
    std::size_t aggregate = inventory.proofInputBytes, retainedBytes = 0;
    for (const auto& relative : inventory.manifests) {
        require(!relative.parent_path().empty(), "selected root cannot itself be a recording session");
        const auto binary = inventory.binarySessions.find(relative);
        auto session = binary == inventory.binarySessions.end()
            ? readSession(root, relative, aggregate, evidence) : binary->second;
        require(identities.insert(session.sessionId).second, "duplicate recording session ID: " + session.sessionId);
        sessionDirectories.insert(session.path);
        const auto parent = relative.parent_path().parent_path();
        if (!parent.empty()) {
            const auto found = groupIndexes.find(parent);
            std::size_t index;
            if (found == groupIndexes.end()) {
                require(result.groups.size() < kRecordingMaxGroups, "recording group count exceeds bound");
                index = result.groups.size(); groupIndexes.emplace(parent, index);
                RecordingGroupInfo group;
                group.path = root.path / parent; group.id = parent.generic_string();
                group.title = parent.filename().string(); group.physical = true;
                result.groups.push_back(std::move(group));
            } else index = found->second;
            auto& group = result.groups[index];
            session.grouped = true; session.groupPath = group.path; session.groupId = group.id;
            session.groupTitle = group.title;
            sessionBounds(session, retainedBytes);
            group.sessions.push_back(session);
        }
        sessionBounds(session, retainedBytes);
        result.sessions.push_back(std::move(session));
    }
    for (auto& group : result.groups) {
        require(!sessionDirectories.contains(group.path), "recording session directory cannot also be a physical group");
        summarize(group);
    }
    for (const auto& relative : inventory.artifacts) {
        const auto found = groupIndexes.find(relative.parent_path());
        require(found != groupIndexes.end(), "orphan group manifest has no direct-child recording sessions");
        std::vector<std::string> ids;
        const auto parsed = parseArtifact(readDocument(root, relative, kRecordingMaxGroupManifestBytes, aggregate, evidence), ids);
        auto& group = result.groups[found->second];
        if (!artifactMatches(root, relative.parent_path(), parsed, ids, group))
            result.refreshRequired.push_back(group.path);
        applyPresentation(group, parsed);
    }
    std::map<fs::path, std::string> titles;
    for (auto& group : result.groups) {
        groupBounds(group, retainedBytes);
        titles.emplace(group.path, group.title);
        for (auto& session : group.sessions) session.groupTitle = group.title;
    }
    for (auto& session : result.sessions)
        if (session.grouped) session.groupTitle = titles.at(session.groupPath);
    std::sort(result.sessions.begin(), result.sessions.end(), [](const auto& left, const auto& right) {
        return left.path.native() < right.path.native();
    });
    std::sort(result.groups.begin(), result.groups.end(), [](const auto& left, const auto& right) {
        return left.path.native() < right.path.native();
    });
    std::sort(result.refreshRequired.begin(), result.refreshRequired.end(), [](const auto& left, const auto& right) {
        return left.native() < right.native();
    });
    result.complete = true;
    checkRootIdentity(root);
    if (snapshot) *snapshot = inventory;
    return result;
}

bool sameSession(const RecordedSessionInfo& left, const RecordedSessionInfo& right) {
    return left.path == right.path && left.manifestPath == right.manifestPath &&
           left.groupPath == right.groupPath && left.sessionId == right.sessionId &&
           left.groupId == right.groupId && left.groupTitle == right.groupTitle &&
           left.exchange == right.exchange && left.market == right.market &&
           left.localSymbol == right.localSymbol && left.folderSymbol == right.folderSymbol &&
           left.normalizedSymbol == right.normalizedSymbol && left.sessionHealth == right.sessionHealth &&
           left.warningSummary == right.warningSummary && left.displayTime == right.displayTime &&
           left.searchText == right.searchText && left.symbols == right.symbols &&
           left.startedAtNs == right.startedAtNs && left.endedAtNs == right.endedAtNs &&
           left.tradesCount == right.tradesCount && left.bookTickerCount == right.bookTickerCount &&
           left.candleCount == right.candleCount && left.totalRows == right.totalRows &&
           left.sourceCount == right.sourceCount && left.binaryCorpus == right.binaryCorpus &&
           left.channelCountsAvailable == right.channelCountsAvailable &&
           left.grouped == right.grouped && left.complete == right.complete;
}

void validate(const Root& root, const RecordingDiscoveryResult& result) {
    require(result.complete && result.errors.empty(), "recording snapshot is incomplete");
    require(result.sessions.size() <= kRecordingMaxSessions && result.groups.size() <= kRecordingMaxGroups,
            "recording snapshot count exceeds bound");
    const auto inventory = inventoryFor(root);
    require(inventory.manifests.size() >= result.sessions.size(), "cached session paths are absent from selected root");
    std::set<fs::path> manifests;
    for (const auto& relative : inventory.manifests) manifests.insert(root.path / relative);
    std::map<std::string, const RecordedSessionInfo*> sessions;
    std::set<fs::path> expectedGroups;
    std::size_t bytes = 0;
    for (const auto& session : result.sessions) {
        sessionBounds(session, bytes);
        require(sessions.emplace(session.sessionId, &session).second, "duplicate cached session ID");
        const auto relative = relativeUnder(root, session.path);
        require(session.path == root.path / relative && session.manifestPath == session.path /
                    (session.binaryCorpus ? "manifest.bin" : "manifest.json") &&
                manifests.erase(session.manifestPath) == 1, "cached session path/root mismatch");
        auto directory = openRelativeDirectory(root, relative);
        const auto parent = session.path.parent_path();
        if (parent == root.path) {
            require(!session.grouped && session.groupPath.empty() && session.groupId.empty() && session.groupTitle.empty(),
                    "root-flat cached session must be ungrouped");
        } else {
            require(session.grouped && session.groupPath == parent &&
                    session.groupId == parent.lexically_relative(root.path).generic_string(),
                    "cached group must be the actual immediate session parent");
            expectedGroups.insert(parent);
        }
    }
    // A cache can predate newly captured sessions. Its own rows and group
    // memberships must be complete and coherent; fresh corpus provenance is
    // established by discover(), never by cached presentation metadata.
    require(expectedGroups.size() == result.groups.size(), "cached group membership is incomplete");
    std::set<std::string> groupedIds;
    for (const auto& group : result.groups) {
        groupBounds(group, bytes);
        const auto relative = relativeUnder(root, group.path);
        require(group.physical && !group.sessions.empty() && group.path == root.path / relative &&
                group.id == relative.generic_string() && expectedGroups.erase(group.path) == 1,
                "cached group path/root/identity mismatch");
        auto directory = openRelativeDirectory(root, relative);
        std::uint64_t rows = 0;
        std::int64_t start = group.sessions.front().startedAtNs, end = 0;
        for (const auto& member : group.sessions) {
            sessionBounds(member, bytes);
            const auto found = sessions.find(member.sessionId);
            require(found != sessions.end() && groupedIds.insert(member.sessionId).second &&
                    sameSession(member, *found->second) && member.grouped && member.groupPath == group.path &&
                    member.path.parent_path() == group.path && member.groupTitle == group.title,
                    "cached group member differs from canonical session row");
            rows = sumRows(rows, member.totalRows);
            start = std::min(start, member.startedAtNs); end = std::max(end, member.endedAtNs);
        }
        require(rows == group.totalRows && start == group.startedAtNs && end == group.endedAtNs,
                "cached group counters/time differ from session summary");
    }
    for (const auto& session : result.sessions)
        require(!session.grouped || groupedIds.contains(session.sessionId), "cached grouped session has no group membership");
    require(result.refreshRequired.size() <= result.groups.size(), "cached refresh list exceeds group count");
    std::set<fs::path> refreshPaths;
    for (const auto& path : result.refreshRequired) {
        accountText(bytes, path.generic_string());
        require(refreshPaths.insert(path).second &&
                std::any_of(result.groups.begin(), result.groups.end(), [&](const auto& group) { return group.path == path; }),
                "cached refresh path is duplicate or is not a current physical group");
    }
    checkRootIdentity(root);
}

std::string renderArtifact(const RecordingGroupInfo& group) {
    std::string document = "{\n  \"schema\": \"hftrec.recording_group\"";
    auto text = [&](const char* key, const std::string& value) {
        document += ",\n  \""; document += key; document += "\": "; document += json::quote(value);
    };
    text("path", group.path.generic_string()); text("id", group.id); text("title", group.title);
    text("local_symbol", group.localSymbol); text("folder_symbol", group.folderSymbol);
    text("normalized_symbol", group.normalizedSymbol); text("display_time", group.displayTime);
    text("search_text", group.searchText);
    document += ",\n  \"started_at_ns\": " + std::to_string(group.startedAtNs);
    document += ",\n  \"ended_at_ns\": " + std::to_string(group.endedAtNs);
    document += ",\n  \"total_rows\": " + std::to_string(group.totalRows);
    document += ",\n  \"physical\": true,\n  \"session_ids\": [";
    bool first = true;
    for (const auto& id : memberIds(group)) {
        if (!first) document += ',';
        first = false; document += json::quote(id);
        require(document.size() <= kRecordingMaxGroupManifestBytes, "rendered group manifest exceeds bound");
    }
    document += "]\n}\n";
    require(document.size() <= kRecordingMaxGroupManifestBytes, "rendered group manifest exceeds bound");
    return document;
}

void atomicWrite(const Root& root, const RecordingGroupInfo& group, const std::string& document,
                 const Inventory& snapshot, const std::vector<FileEvidence>& evidence) {
    const auto relative = relativeUnder(root, group.path);
    auto directory = openRelativeDirectory(root, relative);
    struct stat prior{};
    const int found = ::fstatat(directory.value, kGroupFile, &prior, AT_SYMLINK_NOFOLLOW);
    const bool existed = found == 0;
    require(existed || errno == ENOENT, "cannot inspect existing group manifest");
    require(!existed || S_ISREG(prior.st_mode), "existing group manifest is not a regular file");
    if (existed) {
        std::size_t aggregate = 0; std::vector<std::string> ids;
        const auto parsed = parseArtifact(readDocument(root, relative / kGroupFile,
                                                       kRecordingMaxGroupManifestBytes, aggregate), ids);
        artifactMatches(root, relative, parsed, ids, group);
    }
    checkDirectoryIdentity(root, relative, directory.value);
    static std::atomic<std::uint64_t> sequence{0};
    std::string temporary;
    Fd file;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary = ".recording_group.json.tmp." + std::to_string(static_cast<unsigned long>(::getpid())) +
                    "." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        file = Fd{::openat(directory.value, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
        if (file.value >= 0) break;
        require(errno == EEXIST, "cannot create owned group manifest temporary file");
    }
    require(file.value >= 0, "cannot acquire unique group manifest temporary file");
    struct OwnedTemporary {
        int directory; const std::string& name; bool active{true};
        ~OwnedTemporary() { if (active) ::unlinkat(directory, name.c_str(), 0); }
    } cleanup{directory.value, temporary};
    std::size_t offset = 0;
    while (offset < document.size()) {
        const auto count = ::write(file.value, document.data() + offset, document.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        require(count > 0, "cannot write group manifest temporary file");
        offset += static_cast<std::size_t>(count);
    }
    require(::fsync(file.value) == 0, "cannot synchronize group manifest temporary file");
    verifySnapshot(root, snapshot, evidence);
    checkDirectoryIdentity(root, relative, directory.value);
    struct stat current{};
    const int stillFound = ::fstatat(directory.value, kGroupFile, &current, AT_SYMLINK_NOFOLLOW);
    require(existed ? (stillFound == 0 && S_ISREG(current.st_mode) && current.st_dev == prior.st_dev &&
                       current.st_ino == prior.st_ino && current.st_size == prior.st_size &&
                       current.st_mtim.tv_sec == prior.st_mtim.tv_sec && current.st_mtim.tv_nsec == prior.st_mtim.tv_nsec &&
                       current.st_ctim.tv_sec == prior.st_ctim.tv_sec && current.st_ctim.tv_nsec == prior.st_ctim.tv_nsec)
                    : (stillFound < 0 && errno == ENOENT), "group manifest changed before atomic replacement");
    require(::renameat(directory.value, temporary.c_str(), directory.value, kGroupFile) == 0,
            "cannot atomically replace group manifest");
    cleanup.active = false;
    require(::fsync(directory.value) == 0, "group manifest replaced but directory synchronization failed");
}

void writeValidated(const Root& root, const RecordingGroupInfo& requested) {
    const auto relative = relativeUnder(root, requested.path);
    auto lockedDirectory = openRelativeDirectory(root, relative);
    require(::flock(lockedDirectory.value, LOCK_EX | LOCK_NB) == 0,
            "recording group metadata publication is busy or cannot be locked");
    // The lock belongs to this open directory description and is released on
    // close. Current files and memberships are read only after acquiring it.
    checkDirectoryIdentity(root, relative, lockedDirectory.value);
    Inventory snapshot;
    std::vector<FileEvidence> evidence;
    const auto discovery = discover(root, &snapshot, &evidence);
    validate(root, discovery);
    std::size_t bytes = 0; groupBounds(requested, bytes);
    const auto found = std::find_if(discovery.groups.begin(), discovery.groups.end(), [&](const auto& group) {
        return group.path == requested.path;
    });
    require(found != discovery.groups.end() && requested.physical && requested.id == found->id &&
            requested.startedAtNs == found->startedAtNs && requested.endedAtNs == found->endedAtNs &&
            requested.totalRows == found->totalRows && memberIds(requested) == memberIds(*found),
            "requested group differs from current physical session membership/summary");
    for (const auto& session : requested.sessions) {
        sessionBounds(session, bytes);
        const auto actual = std::find_if(found->sessions.begin(), found->sessions.end(), [&](const auto& row) {
            return row.sessionId == session.sessionId;
        });
        require(actual != found->sessions.end() && session.path == actual->path &&
                session.manifestPath == actual->manifestPath && session.path.parent_path() == requested.path,
                "requested group contains an unproved session path");
    }
    atomicWrite(root, requested, renderArtifact(requested), snapshot, evidence);
}

void errorOut(std::string* output, const char* reason) noexcept {
    if (output) try { *output = reason; } catch (...) {}
}
void clearError(std::string* output) noexcept { if (output) output->clear(); }
void discoveryError(RecordingDiscoveryResult& result, const char* reason) noexcept {
    result.complete = false; result.sessions.clear(); result.groups.clear(); result.refreshRequired.clear();
    try { result.errors.emplace_back(reason); } catch (...) {}
}

}  // namespace

RecordingMetadataReadResult readRecordingMetadata(
    const fs::path& rootPath, const fs::path& path, std::size_t maximumBytes,
    std::string& output, std::string* error, RecordingMetadataReadMode mode,
    std::size_t* consumedBytes) noexcept {
    clearError(error);
    std::size_t consumed = 0;
    if (consumedBytes) *consumedBytes = 0;
    try {
        require(maximumBytes > 0 && maximumBytes <= kRecordingMaxAggregateInputBytes,
                "recording metadata read bound is invalid");
        require(mode == RecordingMetadataReadMode::Document || mode == RecordingMetadataReadMode::FirstNonemptyLine,
                "recording metadata read mode is invalid");
        Root root(rootPath);
        const auto relative = relativeUnder(root, path);
        bool missing = false;
        auto document = readPinnedFile(root, relative, maximumBytes, mode, consumed, missing);
        if (consumedBytes) *consumedBytes = consumed;
        if (missing) { checkRootIdentity(root); return RecordingMetadataReadResult::Missing; }
        output = std::move(document);
        return RecordingMetadataReadResult::Ready;
    } catch (const std::exception& failure) { errorOut(error, failure.what()); }
    catch (...) { errorOut(error, "recording metadata read allocation or I/O failure"); }
    if (consumedBytes) *consumedBytes = consumed;
    return RecordingMetadataReadResult::Refused;
}

std::string recordingDisplayTimestamp(std::int64_t timestampNs) noexcept {
    try {
        if (timestampNs < 0) return {};
        using namespace std::chrono;
        const auto secondsSinceEpoch = seconds{timestampNs / 1000000000};
        const auto day = floor<days>(sys_seconds{secondsSinceEpoch});
        const year_month_day date{day};
        if (!date.ok() || int(date.year()) < 1970 || int(date.year()) > 9999) return {};
        const hh_mm_ss time{sys_seconds{secondsSinceEpoch} - day};
        auto digits = [](std::uint64_t value, std::size_t width) {
            auto text = std::to_string(value); return std::string(width - text.size(), '0') + text;
        };
        return digits(static_cast<unsigned>(int(date.year())), 4) + "-" + digits(unsigned(date.month()), 2) +
               "-" + digits(unsigned(date.day()), 2) + " " + digits(static_cast<std::uint64_t>(time.hours().count()), 2) +
               ":" + digits(static_cast<std::uint64_t>(time.minutes().count()), 2) + ":" +
               digits(static_cast<std::uint64_t>(time.seconds().count()), 2) + "." +
               digits(static_cast<std::uint64_t>(timestampNs % 1000000000), 9) + " UTC";
    } catch (...) { return {}; }
}

std::string recordingGroupFolderName(std::int64_t startedAtNs, const std::string& folderSymbol) noexcept {
    try {
        if (folderSymbol.empty() || folderSymbol.size() > 3 * kRecordingMaxSymbolBytes) return {};
        // Naming owner accepts only canonical uppercase percent encoding.
        if (recordingFolderSymbol(folderSymbol) != folderSymbol) return {};
        const auto display = recordingDisplayTimestamp(startedAtNs);
        if (display.empty()) return {};
        const auto name = display.substr(0, 4) + display.substr(5, 2) + display.substr(8, 2) + "_" +
                          display.substr(11, 2) + display.substr(14, 2) + display.substr(17, 2) + "_" +
                          display.substr(20, 9) + "_" + folderSymbol;
        // A single POSIX path component must fit NAME_MAX as well as PATH_MAX.
        if (name.size() > 255) return {};
        return name;
    } catch (...) { return {}; }
}

RecordingDiscoveryResult discoverRecordings(const fs::path& rootPath) noexcept {
    RecordingDiscoveryResult result;
    try { Root root(rootPath); result = discover(root); validate(root, result); }
    catch (const std::exception& error) { discoveryError(result, error.what()); }
    catch (...) { discoveryError(result, "recording discovery allocation or I/O failure"); }
    return result;
}

bool validateRecordingDiscovery(const fs::path& rootPath, const RecordingDiscoveryResult& discovery,
                                std::string* error) noexcept {
    clearError(error);
    try { Root root(rootPath); validate(root, discovery); return true; }
    catch (const std::exception& failure) { errorOut(error, failure.what()); }
    catch (...) { errorOut(error, "recording validation allocation or I/O failure"); }
    return false;
}

bool writeGroupManifest(const fs::path& rootPath, const RecordingGroupInfo& group, std::string* error) noexcept {
    clearError(error);
    try { Root root(rootPath); writeValidated(root, group); return true; }
    catch (const std::exception& failure) { errorOut(error, failure.what()); }
    catch (...) { errorOut(error, "group manifest write allocation or I/O failure"); }
    return false;
}

bool loadGroupManifest(const fs::path& rootPath, const fs::path& groupPath,
                       RecordingGroupInfo& group, std::string* error) noexcept {
    clearError(error);
    try {
        Root root(rootPath);
        const auto relative = relativeUnder(root, groupPath);
        const auto discovery = discover(root); validate(root, discovery);
        const auto found = std::find_if(discovery.groups.begin(), discovery.groups.end(), [&](const auto& candidate) {
            return candidate.path == root.path / relative;
        });
        require(found != discovery.groups.end(), "group path has no current direct-child recording sessions");
        std::size_t aggregate = 0; std::vector<std::string> ids;
        const auto artifact = parseArtifact(readDocument(root, relative / kGroupFile,
                                                         kRecordingMaxGroupManifestBytes, aggregate), ids);
        artifactMatches(root, relative, artifact, ids, *found);
        auto loaded = *found;
        group = std::move(loaded);
        return true;
    } catch (const std::exception& failure) { errorOut(error, failure.what()); }
    catch (...) { errorOut(error, "group manifest load allocation or I/O failure"); }
    return false;
}

bool writeGroupManifestForPath(const fs::path& rootPath, const fs::path& path, std::string* error) noexcept {
    clearError(error);
    try {
        Root root(rootPath);
        const auto relative = relativeUnder(root, path);
        const auto discovery = discover(root);
        const auto absolute = root.path / relative;
        const auto found = std::find_if(discovery.groups.begin(), discovery.groups.end(), [&](const auto& group) {
            return group.path == absolute || std::any_of(group.sessions.begin(), group.sessions.end(), [&](const auto& session) {
                return session.path == absolute || session.manifestPath == absolute;
            });
        });
        require(found != discovery.groups.end(), "selected path has no physical recording group");
        writeValidated(root, *found);
        return true;
    } catch (const std::exception& failure) { errorOut(error, failure.what()); }
    catch (...) { errorOut(error, "group manifest refresh allocation or I/O failure"); }
    return false;
}

RecordingGroupRefreshResult rebuildRecordingGroups(const fs::path& rootPath, bool apply) noexcept {
    RecordingGroupRefreshResult result;
    try {
        Root root(rootPath);
        result.discovery = discover(root); validate(root, result.discovery);
        for (const auto& group : result.discovery.groups) result.planned.push_back(group.path);
        if (apply) for (const auto& group : result.discovery.groups) {
            // Revalidate every snapshot before each individual atomic replacement.
            writeValidated(root, group);
            result.rebuilt.push_back(group.path);
        }
        if (apply) { result.discovery = discover(root); validate(root, result.discovery); }
        result.complete = true;
    } catch (const std::exception& failure) {
        discoveryError(result.discovery, failure.what());
        try { result.errors.emplace_back(failure.what()); } catch (...) {}
    } catch (...) {
        discoveryError(result.discovery, "group rebuild allocation or I/O failure");
        try { result.errors.emplace_back("group rebuild allocation or I/O failure"); } catch (...) {}
    }
    return result;
}

}  // namespace hftrec::recordings
