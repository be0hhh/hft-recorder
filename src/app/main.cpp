#include <cstdio>
#include <cstring>
#include <string_view>

#include "app/metrics_bootstrap.hpp"
#include "hftrec/version.hpp"

namespace hftrec::app {

int runCapture(int argc, char** argv);
int runAnalyze(int argc, char** argv);
int runReportExport(int argc, char** argv);

}  // namespace hftrec::app

namespace {

void printUsage() {
    std::puts("hft-recorder - support CLI for the GUI-first market-data lab");
    std::puts("");
    std::puts("Usage:");
    std::puts("  hft-recorder <subcommand> [options]");
    std::puts("");
    std::puts("Subcommands:");
    std::puts("  capture   legacy/support capture path");
    std::puts("  analyze   inspect a captured session or derived artifact");
    std::puts("  report    export benchmark and ranking data");
    std::puts("  --version print version and exit");
    std::puts("  --help    print this message");
}

void printVersion() {
    std::printf("hft-recorder %s\n", hftrec::kHftRecorderVersion);
}

}  // namespace

int main(int argc, char** argv) {
    hftrec::app::MetricsBootstrap metricsBootstrap{};
    if (argc < 2) {
        printUsage();
        return 0;
    }
    const std::string_view sub{argv[1]};
    if (sub == "--version" || sub == "-v") { printVersion(); return 0; }
    if (sub == "--help"    || sub == "-h") { printUsage();   return 0; }
    if (sub == "capture")  return hftrec::app::runCapture(argc - 1, argv + 1);
    if (sub == "analyze")  return hftrec::app::runAnalyze(argc - 1, argv + 1);
    if (sub == "report")   return hftrec::app::runReportExport(argc - 1, argv + 1);

    std::fprintf(stderr, "unknown subcommand: %.*s\n",
                 static_cast<int>(sub.size()), sub.data());
    printUsage();
    return 2;
}
