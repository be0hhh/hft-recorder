#include "Tui/RecorderTuiInternal.hpp"

namespace hftrec::app {
using namespace tui_detail;

int runTui(int argc, char** argv) {
    tui::RecorderTuiPreset preset{};
    std::filesystem::path presetPath = tui::defaultPresetPath();
    bool explicitPreset = false;
    bool explicitOutputDir = false;
    bool explicitProgressSec = false;
    std::filesystem::path outputDirOverride;
    int progressSecOverride = preset.progressSec;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--preset") {
            if (i + 1 >= argc) {
                std::fputs("tui: --preset requires a path\n", stderr);
                return 2;
            }
            presetPath = tui::resolvePresetPath(argv[++i]);
            explicitPreset = true;
            continue;
        }
        if (arg == "--output-dir") {
            if (i + 1 >= argc) {
                std::fputs("tui: --output-dir requires a path\n", stderr);
                return 2;
            }
            outputDirOverride = argv[++i];
            explicitOutputDir = true;
            continue;
        }
        if (arg == "--progress-sec") {
            if (i + 1 >= argc) {
                std::fputs("tui: --progress-sec requires a value\n", stderr);
                return 2;
            }
            progressSecOverride = std::max(1, std::atoi(argv[++i]));
            explicitProgressSec = true;
            continue;
        }
        std::fprintf(stderr, "tui: unknown option '%.*s'\n", static_cast<int>(arg.size()), arg.data());
        printUsage();
        return 2;
    }

    std::string message;
    if (std::filesystem::exists(presetPath)) {
        if (!tui::loadPresetFile(presetPath, preset, message)) {
            if (explicitPreset) {
                std::fprintf(stderr, "tui: %s\n", message.c_str());
                return 1;
            }
            message = "default preset ignored: " + message;
        } else {
            message = "loaded " + presetPath.string();
        }
    }

    if (explicitOutputDir) preset.outputDir = outputDirOverride;
    if (explicitProgressSec) preset.progressSec = progressSecOverride;
    if (preset.jobs.empty()) addDefaultJob(preset);

    TerminalGuard terminal;
    if (!terminal.interactive()) {
        std::fputs("tui: interactive terminal is required\n", stderr);
        return 2;
    }
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::size_t selected = 0;
    bool dirty = true;
    while (!gInterrupted) {
        if (selected >= preset.jobs.size()) {
            selected = preset.jobs.empty() ? 0 : preset.jobs.size() - 1u;
            dirty = true;
        }
        if (dirty) {
            renderMainMenu(preset, selected, presetPath, message);
            message.clear();
            dirty = false;
        }
        const Key key = readKey(250);
        if (key.kind == KeyKind::None) continue;
        if (key.kind == KeyKind::Up && selected > 0) {
            --selected;
            dirty = true;
        } else if (key.kind == KeyKind::Down && selected + 1u < preset.jobs.size()) {
            ++selected;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'a') {
            addDefaultJob(preset);
            selected = preset.jobs.size() - 1u;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'g') {
            const std::string input = promptLine(terminal, "symbols or .ini list");
            if (input.empty()) {
                message = "symbol generation canceled";
                dirty = true;
                continue;
            }
            const std::size_t before = preset.jobs.size();
            const GeneratedJobsAppendResult result = appendGeneratedSymbolJobs(preset, input);
            if (preset.jobs.size() > before) selected = before;
            message = generatedJobsMessage(result, "added");
            dirty = true;
        } else if (key.kind == KeyKind::Enter) {
            if (!preset.jobs.empty()) editJob(terminal, preset.jobs[selected]);
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'c') {
            duplicateJob(preset, selected);
            if (!preset.jobs.empty()) ++selected;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'd') {
            if (!preset.jobs.empty()) preset.jobs.erase(preset.jobs.begin() + static_cast<std::ptrdiff_t>(selected));
            dirty = true;
        } else if (key.kind == KeyKind::Character && (key.ch == 'w' || key.ch == 'W')) {
            std::string error;
            message = tui::savePresetFile(presetPath, preset, error) ? "saved " + presetPath.string() : error;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 's') {
            const std::string maybePath = promptLine(terminal, "save preset as path");
            if (maybePath.empty()) {
                message = "save as canceled";
            } else {
                const std::filesystem::path savePath = tui::resolvePresetPath(maybePath);
                std::string error;
                if (tui::savePresetFile(savePath, preset, error)) {
                    presetPath = savePath;
                    message = "saved " + presetPath.string();
                } else {
                    message = error;
                }
            }
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'l') {
            const std::string maybePath = promptLine(terminal, "load preset path", presetPath.string());
            if (!maybePath.empty()) presetPath = tui::resolvePresetPath(maybePath);
            std::string error;
            message = tui::loadPresetFile(presetPath, preset, error) ? "loaded " + presetPath.string() : error;
            dirty = true;
        } else if (key.kind == KeyKind::Character && key.ch == 'r') {
            if (preset.jobs.empty()) {
                message = "add at least one job";
                dirty = true;
            } else {
                (void)runShardPresetInteractive(preset, presetPath);
                dirty = true;
            }
        } else if (key.kind == KeyKind::Character && key.ch == 'R') {
            if (preset.jobs.empty()) {
                message = "add at least one job";
                dirty = true;
            } else {
                runJobs(terminal, preset);
                dirty = true;
            }
        } else if (key.kind == KeyKind::Character && key.ch == 'q') {
            break;
        }
    }
    return 0;
}

}  // namespace hftrec::app
