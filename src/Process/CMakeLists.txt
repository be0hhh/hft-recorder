# src/Process/ — CLI entrypoints.
#
# Produces two executables:
#   hft-recorder        — main CLI (capture / analyze subcommands)
#   history             — Binance Vision offline importer

add_executable(hft-recorder
    src/Main.cpp
    src/CaptureCli.cpp
    src/TuiCli.cpp
    src/Tui/RecorderTuiTerminal.cpp
    src/Tui/RecorderTuiPresetEditor.cpp
    src/Tui/RecorderTuiJobs.cpp
    src/Tui/RecorderTuiDeadSessions.cpp
    src/Tui/RecorderTuiRendering.cpp
    src/PresetRunnerCli.cpp
    src/ShardRunCli.cpp
    src/AnalyzeCli.cpp
    src/RecordingsCli.cpp
    src/ParserCaptureCli.cpp
)

target_include_directories(hft-recorder
    PRIVATE
        ${HFTREC_SOURCE_DIR}/src/Runtime/include
        ${HFTREC_SOURCE_DIR}/src/Runtime/src
        ${HFTREC_SOURCE_DIR}/src/Process/src
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
    src/HistoryCli.cpp
)

target_include_directories(history
    PRIVATE
        ${HFTREC_SOURCE_DIR}/src/Runtime/include
        ${HFTREC_SOURCE_DIR}/src/Runtime/src
        ${HFTREC_SOURCE_DIR}/src/Process/src
)

target_link_libraries(history
    PRIVATE
        hftrec_core
        OpenSSL::Crypto
)

target_compile_features(history PRIVATE cxx_std_23)
