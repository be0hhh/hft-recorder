# src/app/ — CLI entrypoints.
#
# Produces two executables:
#   hft-recorder        — main CLI (capture / analyze subcommands)
#   hft-recorder-bench  — offline bench tool
#   history             — Binance Vision offline importer

add_executable(hft-recorder
    main.cpp
    metrics_bootstrap.cpp
    capture_cli.cpp
    tui_cli.cpp
    preset_runner_cli.cpp
    shard_run_cli.cpp
    analyze_cli.cpp
    report_export.cpp
    recordings_cli.cpp
)

target_include_directories(hft-recorder
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(hft-recorder
    PRIVATE
        hftrec_core
        hftrec_support
        hftrec_variant_trade_var01
        hftrec_variant_l1_var01
        hftrec_variant_orderbook_var01
)

target_compile_features(hft-recorder PRIVATE cxx_std_23)

find_package(OpenSSL REQUIRED)

add_executable(history
    history_cli.cpp
)

target_include_directories(history
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(history
    PRIVATE
        hftrec_core
        OpenSSL::Crypto
)

target_compile_features(history PRIVATE cxx_std_23)

add_executable(hft-recorder-bench
    benchmark_cli.cpp
)

target_include_directories(hft-recorder-bench
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(hft-recorder-bench
    PRIVATE
        hftrec_core
        hftrec_support
        hftrec_variant_trade_var01
        hftrec_variant_l1_var01
        hftrec_variant_orderbook_var01
        hft_compressor_core
)

target_compile_features(hft-recorder-bench PRIVATE cxx_std_23)


