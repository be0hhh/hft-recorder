# src/core/ — stable shared library for hft-recorder.
# Compiled as a STATIC lib so the app and variants can link against it.

add_library(hftrec_core STATIC
    capture/SessionId.cpp
    capture/SessionManifest.cpp
    capture/ChannelJsonWriter.cpp
    capture/CaptureCoordinator.cpp
    capture/JsonSerializers.cpp
    common/Log.cpp
    codec/VarInt.cpp
    codec/Crc32c.cpp
    corpus/CorpusLoader.cpp
    cxet_bridge/CxetCaptureBridge.cpp
    block/FileHeader.cpp
    block/BlockHeader.cpp
    block/BlockWriter.cpp
    block/BlockReader.cpp
    lab/LabRunner.cpp
    lab/RankingEngine.cpp
    metrics/Metrics.cpp
    validation/ValidationRunner.cpp
)

target_include_directories(hftrec_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hftrec_core
    PUBLIC
        cxet_lib
        Threads::Threads
        spdlog::spdlog
        fmt::fmt
)

target_compile_features(hftrec_core PUBLIC cxx_std_23)
