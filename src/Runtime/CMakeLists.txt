# src/Runtime/ - stable shared library for hft-recorder.
# Compiled as a STATIC lib so the app and variants can link against it.

find_package(Threads REQUIRED)

# One production owner for JSON capture, durable session storage and replay.
# The offline executable supplies only its retained native boundary fixture;
# Full keeps NativeMarketCapture in hftrec_core alongside these same objects.
add_library(hftrec_json_capture OBJECT
    src/Capture/Session/SessionId.cpp
    src/Capture/Serialization/ChannelJsonWriter.cpp
    src/Capture/Serialization/JsonSerializers.cpp
    src/Capture/Session/SupportArtifacts.cpp
    src/Capture/Coordinator/CaptureChannelSupport.cpp
    src/Capture/Coordinator/CaptureCoordinatorInternal.cpp
    src/Capture/Coordinator/CaptureCoordinator.cpp
    src/Capture/Coordinator/CaptureCoordinatorRuntime.cpp
    src/Capture/Coordinator/CaptureCoordinatorRuntimeLoops.cpp
    src/Capture/Coordinator/VenueMultiplexCapture.cpp
    src/Corpus/CorpusLoader.cpp
    src/Corpus/InstrumentMetadata.cpp
    src/Corpus/Recordings/RecordingRoot.cpp
    src/Corpus/Recordings/RecordingDiscovery.cpp
    src/Corpus/Storage/EventStorage.cpp
    src/Corpus/Storage/JsonSessionStorage.cpp
    src/Replay/BookState.cpp
    src/Replay/SessionReplay.cpp
    src/Replay/SessionReplayTimeline.cpp
    ../Process/src/Finam/FinamEnvSync.cpp
)
target_include_directories(hftrec_json_capture
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hftrec_json_capture
    PUBLIC hftrec::corpus_contract cxet::cxet_lib Threads::Threads absl::flat_hash_map)
target_compile_definitions(hftrec_json_capture PUBLIC HFTREC_WITH_CXET=1)
target_compile_features(hftrec_json_capture PUBLIC cxx_std_23)
target_compile_options(hftrec_json_capture PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-variable)

if(CXET_LOCAL_TESTS)
    cxet_offline_instrument(hftrec_json_capture)
    return()
endif()

set(HFTREC_CORE_SOURCES
    ../Lab/src/Arbitrage/BookTickerSpread.cpp
    ../Lab/src/Arbitrage/BookTickerSpreadMean.cpp
    ../Lab/src/Arbitrage/CandleSpread.cpp
    src/Capture/Serialization/Candles2BulkState.cpp
    src/Capture/Coordinator/RecorderCaptureSession.cpp
    src/Corpus/Codec/VarInt.cpp
    src/Execution/ExecutionVenue.cpp
    src/History/BinanceVisionFormat.cpp
    src/History/ZipReader.cpp
    src/Corpus/Block/FileHeader.cpp
    src/Corpus/Block/BlockHeader.cpp
    src/Corpus/Block/BlockWriter.cpp
    src/Corpus/Block/BlockReader.cpp
    ../Lab/src/BookFrameSampler.cpp
    ../Lab/src/LabRunner.cpp
    ../Lab/src/RankingEngine.cpp
    src/Replay/RecorderApi.cpp
    src/Replay/RecorderReplaySelection.cpp
    src/Replay/RecorderReplayRows.cpp
    src/Corpus/Recordings/BasisChainManifest.cpp
    src/Corpus/Recordings/BasisChainSeries.cpp
    ../Process/src/Tui/RecorderTuiLaunch.cpp
    ../Process/src/Tui/RecorderTuiPreset.cpp
    ../Process/src/Tui/RecorderTuiShard.cpp
    ../Process/src/Tui/RecorderTuiSymbols.cpp
    ../Process/src/Tui/Status/RecorderTuiShardStatus.cpp
    ../Process/src/Tui/TerminalRender.cpp
    src/Validation/ValidationRunner.cpp
)

list(APPEND HFTREC_CORE_SOURCES
    src/Capture/Coordinator/CaptureCoordinatorBulk.cpp
    src/Capture/Session/MarketData/NativeMarketCapture.cpp
    src/Capture/Session/Bridge/CxetCaptureBridge.cpp
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
        hftrec::capture_launch
        hftrec::capture_client
        hftp_client_headers
        cxet::cxet_lib
        Threads::Threads
        absl::flat_hash_map
        ZLIB::ZLIB
)

target_compile_definitions(hftrec_core PUBLIC HFTREC_WITH_CXET=1)

target_compile_features(hftrec_core PUBLIC cxx_std_23)
target_link_libraries(hftrec_recorder_api INTERFACE hftrec_core)
target_compile_features(hftrec_recorder_api INTERFACE cxx_std_23)
