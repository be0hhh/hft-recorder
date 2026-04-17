# src/app/ — CLI entrypoints.
#
# Produces two executables:
#   hft-recorder        — main CLI (capture / analyze subcommands)
#   hft-recorder-bench  — offline bench tool

add_executable(hft-recorder
    main.cpp
    capture_cli.cpp
    analyze_cli.cpp
    report_export.cpp
)

target_include_directories(hft-recorder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
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

add_executable(hft-recorder-bench
    benchmark_cli.cpp
)

target_include_directories(hft-recorder-bench
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hft-recorder-bench
    PRIVATE
        hftrec_core
        hftrec_support
        hftrec_variant_trade_var01
        hftrec_variant_l1_var01
        hftrec_variant_orderbook_var01
)

target_compile_features(hft-recorder-bench PRIVATE cxx_std_23)
