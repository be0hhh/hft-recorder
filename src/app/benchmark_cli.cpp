#include <cstdio>
#include <string_view>

#include "hftrec/version.hpp"

namespace {

void printHelp() {
    std::puts("hft-recorder-bench - support CLI for corpus-based compression experiments");
    std::puts("");
    std::puts("Usage:");
    std::puts("  hft-recorder-bench <session_dir> [--pipelines all|id1,id2,...]");
    std::puts("");
    std::puts("This binary is secondary to the GUI-first workflow.");
    std::puts("The canonical path is: capture JSON corpus -> validate -> run lab -> view rankings.");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { printHelp(); return 0; }
    const std::string_view a1{argv[1]};
    if (a1 == "--help" || a1 == "-h") { printHelp(); return 0; }
    if (a1 == "--version" || a1 == "-v") {
        std::printf("hft-recorder-bench %s\n", hftrec::kHftRecorderVersion);
        return 0;
    }
    std::printf("hft-recorder-bench: session=%s (skeleton only; GUI-first backend in progress)\n", argv[1]);
    return 0;
}
