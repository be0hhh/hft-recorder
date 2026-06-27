#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"

namespace hftrec::app {
namespace {

void printUsage() {
    std::puts("Usage:");
    std::puts("  hft-recorder recordings organize [--root path] [--apply] [--window-sec n]");
    std::puts("");
    std::puts("Default mode is dry-run. Add --apply to move completed flat sessions.");
}

}  // namespace

int runRecordings(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h") {
        printUsage();
        return 0;
    }
    const std::string_view command{argv[1]};
    if (command != "organize") {
        std::fprintf(stderr, "recordings: unknown command '%.*s'\n", static_cast<int>(command.size()), command.data());
        printUsage();
        return 2;
    }

    std::filesystem::path root{recordings::defaultRecordingsRoot()};
    bool apply = false;
    std::int64_t windowSec = 300;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--root") {
            if (i + 1 >= argc) {
                std::fputs("recordings organize: --root requires a path\n", stderr);
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
        if (arg == "--window-sec") {
            if (i + 1 >= argc) {
                std::fputs("recordings organize: --window-sec requires a value\n", stderr);
                return 2;
            }
            windowSec = std::max<std::int64_t>(1, std::atoll(argv[++i]));
            continue;
        }
        std::fprintf(stderr, "recordings organize: unknown option '%.*s'\n", static_cast<int>(arg.size()), arg.data());
        return 2;
    }

    const auto result = recordings::organizeRecordings(root, apply, windowSec);
    std::printf("recordings organize / root=%s mode=%s window=%llds moves=%zu skipped_active=%zu errors=%zu\n",
                root.string().c_str(),
                apply ? "apply" : "dry-run",
                static_cast<long long>(windowSec),
                result.moves.size(),
                result.skippedActive.size(),
                result.errors.size());

    for (const auto& move : result.moves) {
        std::printf("%s %s -> %s\n",
                    move.moved ? "moved" : "plan ",
                    move.from.string().c_str(),
                    move.to.string().c_str());
        if (!move.error.empty()) std::printf("  error: %s\n", move.error.c_str());
    }
    for (const auto& skipped : result.skippedActive) {
        std::printf("skip active/incomplete: %s\n", skipped.c_str());
    }
    for (const auto& error : result.errors) {
        std::printf("error: %s\n", error.c_str());
    }
    return result.errors.empty() ? 0 : 1;
}

}  // namespace hftrec::app
