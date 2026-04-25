# src/core/ - stable shared library for hft-recorder.
# Compiled as a STATIC lib so the app and variants can link against it.

set(HFTREC_CORE_SOURCES
    capture/SessionId.cpp
    capture/SessionManifest.cpp
    capture/ChannelJsonWriter.cpp
    capture/CaptureCoordinatorInternal.cpp
    capture/CaptureCoordinator.cpp
    capture/JsonSerializers.cpp
    capture/SupportArtifacts.cpp
    common/Log.cpp
    codec/VarInt.cpp
    codec/Crc32c.cpp
    corpus/CorpusLoader.cpp
    corpus/InstrumentMetadata.cpp
    execution/ExecutionVenue.cpp
    block/FileHeader.cpp
    block/BlockHeader.cpp
    block/BlockWriter.cpp
    block/BlockReader.cpp
    lab/BookFrameSampler.cpp
    lab/LabRunner.cpp
    lab/RankingEngine.cpp
    metrics/Metrics.cpp
    replay/JsonLineParser.cpp
    replay/BookState.cpp
    replay/SessionReplay.cpp
    replay/SessionReplayTimeline.cpp
    storage/EventStorage.cpp
    storage/JsonSessionStorage.cpp
    validation/ValidationRunner.cpp
)

if(HFTREC_WITH_CXET)
    list(APPEND HFTREC_CORE_SOURCES
        capture/CaptureCoordinatorRuntime.cpp
        cxet_bridge/CxetCaptureBridge.cpp
        local_exchange/LocalExchangeServer.cpp
        local_exchange/LocalOrderEngine.cpp
    )
else()
    list(APPEND HFTREC_CORE_SOURCES
        capture/CaptureCoordinatorOffline.cpp
    )
endif()

add_library(hftrec_core STATIC ${HFTREC_CORE_SOURCES})

target_include_directories(hftrec_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hftrec_core
    PUBLIC
        Threads::Threads
        spdlog::spdlog
        fmt::fmt
)

if(HFTREC_WITH_CXET)
    target_link_libraries(hftrec_core PUBLIC cxet_lib)
    target_compile_definitions(hftrec_core PUBLIC HFTREC_WITH_CXET=1)
else()
    target_compile_definitions(hftrec_core PUBLIC HFTREC_WITH_CXET=0)
endif()

target_compile_features(hftrec_core PUBLIC cxx_std_23)