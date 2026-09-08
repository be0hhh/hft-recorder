# src/Runtime/ - stable shared library for hft-recorder.
# Compiled as a STATIC lib so the app and variants can link against it.

set(HFTREC_CORE_SOURCES
    ../Lab/src/Arbitrage/BookTickerSpread.cpp
    ../Lab/src/Arbitrage/BookTickerSpreadMean.cpp
    ../Lab/src/Arbitrage/CandleSpread.cpp
    src/Capture/SessionId.cpp
    src/Capture/Candles2BulkState.cpp
    src/Capture/ChannelJsonWriter.cpp
    src/Capture/ParserMarketCaptureClient.cpp
    src/Capture/CaptureChannelSupport.cpp
    src/Capture/CaptureCoordinatorInternal.cpp
    src/Capture/CaptureCoordinator.cpp
    src/Capture/JsonSerializers.cpp
    src/Capture/SupportArtifacts.cpp
    src/Common/Log.cpp
    src/Corpus/Codec/VarInt.cpp
    src/Corpus/CorpusLoader.cpp
    src/Corpus/InstrumentMetadata.cpp
    src/Execution/ExecutionVenue.cpp
    ../Process/src/Finam/FinamEnvSync.cpp
    src/History/BinanceVisionFormat.cpp
    src/History/ZipReader.cpp
    src/Corpus/Block/FileHeader.cpp
    src/Corpus/Block/BlockHeader.cpp
    src/Corpus/Block/BlockWriter.cpp
    src/Corpus/Block/BlockReader.cpp
    ../Lab/src/BookFrameSampler.cpp
    ../Lab/src/LabRunner.cpp
    ../Lab/src/RankingEngine.cpp
    src/Metrics/Metrics.cpp
    src/Replay/RecorderApi.cpp
    src/Replay/RecorderReplaySelection.cpp
    src/Replay/RecorderReplayRows.cpp
    src/Replay/BookState.cpp
    src/Replay/CxetReplaySessionLoader.cpp
    src/Replay/SessionReplay.cpp
    src/Replay/SessionReplayTimeline.cpp
    src/Corpus/Recordings/BasisChainManifest.cpp
    src/Corpus/Recordings/BasisChainSeries.cpp
    src/Corpus/Recordings/RecordingDiscovery.cpp
    src/Corpus/Recordings/RecordingRoot.cpp
    src/Corpus/Storage/EventStorage.cpp
    src/Corpus/Storage/JsonSessionStorage.cpp
    ../Process/src/Tui/RecorderTuiLaunch.cpp
    ../Process/src/Tui/RecorderTuiPreset.cpp
    ../Process/src/Tui/RecorderTuiShard.cpp
    ../Process/src/Tui/RecorderTuiSymbols.cpp
    ../Process/src/Tui/Status/RecorderTuiShardStatus.cpp
    ../Process/src/Tui/TerminalRender.cpp
    src/Validation/ValidationRunner.cpp
)

if(HFTREC_WITH_CXET)
    list(APPEND HFTREC_CORE_SOURCES
        src/Capture/CaptureCoordinatorBulk.cpp
        src/Capture/CaptureCoordinatorRuntime.cpp
        src/Capture/CaptureCoordinatorRuntimeLoops.cpp
        src/Capture/VenueMultiplexCapture.cpp
        src/Capture/Bridge/CxetCaptureBridge.cpp
    )
else()
    list(APPEND HFTREC_CORE_SOURCES
        src/Capture/CaptureCoordinatorOffline.cpp
    )
endif()

add_library(hftrec_core STATIC ${HFTREC_CORE_SOURCES})
add_library(hftrec::core ALIAS hftrec_core)
add_library(hftrec_recorder_api INTERFACE)
add_library(hftrec::recorder_api ALIAS hftrec_recorder_api)

target_include_directories(hftrec_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src/Runtime/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/Runtime/src
        ${HFT_PARSER_CONTRACT_INCLUDE_DIR}
)

target_link_libraries(hftrec_core
    PUBLIC
        hftrec_corpus_contract
        Threads::Threads
        absl::flat_hash_map
        spdlog::spdlog
        fmt::fmt
        ZLIB::ZLIB
)

if(HFTREC_WITH_TRADER_RUNTIME)
    target_link_libraries(hftrec_core PUBLIC hft_trader_runtime)
    target_compile_definitions(hftrec_core PUBLIC HFTREC_WITH_TRADER_RUNTIME=1 HFTREC_WITH_CXET=1)
else()
    target_compile_definitions(hftrec_core PUBLIC HFTREC_WITH_TRADER_RUNTIME=0 HFTREC_WITH_CXET=0)
endif()

if(HFTREC_WITH_CXET_REPLAY)
    target_link_libraries(hftrec_core PRIVATE cxet_replay_core)
    target_compile_definitions(hftrec_core PUBLIC HFTREC_WITH_CXET_REPLAY=1)
else()
    target_compile_definitions(hftrec_core PUBLIC HFTREC_WITH_CXET_REPLAY=0)
endif()

target_compile_definitions(hftrec_core PUBLIC HFTREC_HOTPATH_METRICS_DEFAULT=${HFTREC_HOTPATH_METRICS_DEFAULT_VALUE})

target_compile_features(hftrec_core PUBLIC cxx_std_23)
target_link_libraries(hftrec_recorder_api INTERFACE hftrec_core)
target_compile_features(hftrec_recorder_api INTERFACE cxx_std_23)

