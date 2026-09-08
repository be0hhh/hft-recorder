# tests/ — three targets per docs/TESTING_CONTRACT.md.
#   hftrec_unit_tests         — ctest default
#   hftrec_integration_tests  — ctest default
#   hftrec_benchmarks         — opt-in via `ctest -L bench`

add_subdirectory(Unit)
add_subdirectory(Integration)
add_subdirectory(Bench)
