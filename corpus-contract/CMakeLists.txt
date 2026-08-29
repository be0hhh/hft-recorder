# Recorder-owned canonical corpus contract shared with offline consumers.
# Keep this target independent from Qt, capture runtimes, and hft-trader state.

add_library(hftrec_corpus_contract STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/capture/SessionManifest.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/codec/Crc32c.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/corpus/BinaryMarketCorpusReader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/corpus/BinaryMarketCorpusWriter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/replay/JsonLineParser.cpp
)
add_library(hftrec::corpus_contract ALIAS hftrec_corpus_contract)
set_target_properties(hftrec_corpus_contract PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(hftrec_corpus_contract
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../src
)
target_compile_features(hftrec_corpus_contract PUBLIC cxx_std_23)
