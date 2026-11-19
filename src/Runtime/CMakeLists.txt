# src/Runtime/ - stable shared library for hft-recorder.
# Compiled as a STATIC lib so the app and variants can link against it.

find_package(Threads REQUIRED)

# One cold owner for bounded recording discovery and group metadata. Naming
# and root policy stay compiled once, shared by capture, CLI, GUI and tests.
add_library(hftrec_recording_groups STATIC
    src/Corpus/Recordings/RecordingGroups.cpp
    src/Corpus/Recordings/RecordingDiscovery.cpp
    src/Corpus/Recordings/RecordingRoot.cpp)
add_library(hftrec::recording_groups ALIAS hftrec_recording_groups)
target_include_directories(hftrec_recording_groups
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hftrec_recording_groups
    PUBLIC hftrec::corpus_contract cxet::cxet_lib)
target_compile_features(hftrec_recording_groups PUBLIC cxx_std_23)
set_target_properties(hftrec_recording_groups PROPERTIES POSITION_INDEPENDENT_CODE ON)

# One production owner for JSON capture, durable session storage and replay.
# The offline executable supplies only its retained native boundary fixture;
# Full keeps NativeMarketCapture in hftrec_core alongside these same objects.
add_library(hftrec_json_capture OBJECT
    src/Capture/Session/SessionId.cpp
    src/Capture/Session/SupportArtifacts.cpp
    src/Corpus/InstrumentMetadata.cpp
)
target_include_directories(hftrec_json_capture
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hftrec_json_capture
    PUBLIC hftrec::corpus_contract hftrec::recording_groups cxet::cxet_lib Threads::Threads absl::flat_hash_map)
target_compile_definitions(hftrec_json_capture PUBLIC HFTREC_WITH_CXET=1)
target_compile_features(hftrec_json_capture PUBLIC cxx_std_23)
target_compile_options(hftrec_json_capture PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-variable)

if(CXET_LOCAL_TESTS)
    cxet_offline_instrument(hftrec_recording_groups)
    cxet_offline_instrument(hftrec_json_capture)
    return()
endif()

set(HFTREC_CORE_SOURCES
    src/History/BinanceVisionFormat.cpp
    src/History/ZipReader.cpp
    ../Process/src/Tui/RecorderTuiLaunch.cpp
    ../Process/src/Tui/RecorderTuiPreset.cpp
    ../Process/src/Tui/RecorderTuiShard.cpp
    ../Process/src/Tui/RecorderTuiSymbols.cpp
    ../Process/src/Tui/Status/RecorderTuiShardStatus.cpp
    ../Process/src/Tui/TerminalRender.cpp
    src/Validation/ValidationRunner.cpp
)

add_library(hftrec_core STATIC ${HFTREC_CORE_SOURCES})
add_library(hftrec::core ALIAS hftrec_core)
add_library(hftrec_recorder_api INTERFACE)
add_library(hftrec::recorder_api ALIAS hftrec_recorder_api)

target_include_directories(hftrec_core
    PUBLIC
        ${HFTREC_SOURCE_DIR}/src/Runtime/include
    PRIVATE
        ${HFTREC_SOURCE_DIR}/src/Runtime/src
)

target_link_libraries(hftrec_core
    PUBLIC
        hftrec_json_capture
        hftrec::corpus_contract
        hftrec::capture_client
        hftp_client_headers
        hft_parser::current_reference_client
        cxet::cxet_lib
        Threads::Threads
        absl::flat_hash_map
        ZLIB::ZLIB
)

target_compile_definitions(hftrec_core PUBLIC HFTREC_WITH_CXET=1)

target_compile_features(hftrec_core PUBLIC cxx_std_23)
target_link_libraries(hftrec_recorder_api INTERFACE hftrec_core)
target_compile_features(hftrec_recorder_api INTERFACE cxx_std_23)
