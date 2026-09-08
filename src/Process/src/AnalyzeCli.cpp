#include <cstdio>

namespace hftrec::app {

int runAnalyze(int /*argc*/, char** /*argv*/) {
    std::puts("hft-recorder analyze — not implemented yet (Phase 2).");
    std::puts("This subcommand will open a .cxrec, verify CRCs, and print per-block stats.");
    return 0;
}

}  // namespace hftrec::app
