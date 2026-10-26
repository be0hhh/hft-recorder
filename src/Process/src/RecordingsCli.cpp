#include <cstdio>
#include <filesystem>
#include <string_view>

#include "Corpus/Recordings/RecordingGroups.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"

namespace hftrec::app {
namespace {

void printUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder recordings refresh [--root path] [--dry-run|--apply]");
    std::puts("");
    std::puts("Default mode scans the catalog and plans group metadata updates.");
    std::puts("--apply rebuilds current group metadata. Recording paths and session files stay in place.");
}

}  // namespace

int runRecordings(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h") {
        printUsage();
        return 0;
    }
    const std::string_view command{argv[1]};
    if(command=="organize") {
        std::fputs("recordings organize is no longer supported; use recordings refresh for catalog and group metadata updates\n",stderr);
        return 2;
    }
    if (command != "refresh") {
        std::fprintf(stderr, "recordings: unknown command '%.*s'\n", static_cast<int>(command.size()), command.data());
        printUsage();
        return 2;
    }

    std::filesystem::path root{recordings::defaultRecordingsRoot()};
    bool apply = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--root") {
            if (i + 1 >= argc) {
                std::fputs("recordings refresh: --root requires a path\n", stderr);
                return 2;
            }
            root = recordings::normalizeExplicitRecordingsPath(argv[++i]);
            continue;
        }
        if (arg == "--apply") {
            apply = true;
            continue;
        }
        if (arg == "--dry-run") {
            apply = false;
            continue;
        }
        if (arg == "--delete-empty" || arg == "--window-sec") {
            std::fputs("recordings refresh: deletion, quarantine and time-based regrouping are not supported\n",stderr);
            return 2;
        }
        std::fprintf(stderr, "recordings refresh: unknown option '%.*s'\n", static_cast<int>(arg.size()), arg.data());
        return 2;
    }

    const auto result = recordings::rebuildRecordingGroups(root,apply);
    std::printf("recordings refresh / root=%s mode=%s complete=%s sessions=%zu groups=%zu planned=%zu rebuilt=%zu errors=%zu\n",
                root.string().c_str(),
                apply ? "apply" : "dry-run",
                result.complete?"yes":"no",
                result.discovery.sessions.size(),
                result.discovery.groups.size(),
                result.planned.size(),
                result.rebuilt.size(),
                result.errors.size());

    if(!apply) for(const auto& path:result.planned) std::printf("plan group metadata: %s\n",path.string().c_str());
    for(const auto& path:result.rebuilt) std::printf("rebuilt group metadata: %s\n",path.string().c_str());
    for (const auto& error : result.errors) {
        std::printf("error: %s\n", error.c_str());
    }
    return result.complete && result.errors.empty() ? 0 : 1;
}

}  // namespace hftrec::app
