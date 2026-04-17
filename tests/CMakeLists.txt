# tests/ — three targets per doc/TESTING_CONTRACT.md.
#   hftrec_unit_tests         — ctest default
#   hftrec_integration_tests  — ctest default
#   hftrec_benchmarks         — opt-in via `ctest -L bench`

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(bench)
