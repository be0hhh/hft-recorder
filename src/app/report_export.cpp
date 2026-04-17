#include <cstdio>

namespace hftrec::app {

int runReportExport(int /*argc*/, char** /*argv*/) {
    std::puts("hft-recorder report — not implemented yet (Phase 3).");
    std::puts("This subcommand will pipe bench results to CSV / Prometheus pushgateway.");
    return 0;
}

}  // namespace hftrec::app
