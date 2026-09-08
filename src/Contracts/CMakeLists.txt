# Recorder-owned canonical corpus contract shared with offline consumers.
# Keep this target independent from Qt, capture runtimes, and hft-trader state.

add_library(hftrec_corpus_contract STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Capture/SessionManifest.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Corpus/Codec/Crc32c.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Corpus/BinaryMarketCorpusReader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Corpus/BinaryMarketCorpusWriter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Replay/JsonLineParser.cpp
)
add_library(hftrec::corpus_contract ALIAS hftrec_corpus_contract)
set_target_properties(hftrec_corpus_contract PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(hftrec_corpus_contract
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src
)
target_compile_features(hftrec_corpus_contract PUBLIC cxx_std_23)

if(CXET_SANITIZER_PROFILE STREQUAL "address-undefined")
    target_compile_options(hftrec_corpus_contract PRIVATE
        -O2 -g3 -fsanitize=address,undefined
        -fsanitize-address-use-after-scope -fno-omit-frame-pointer
        -fno-optimize-sibling-calls -fno-sanitize-recover=all)
    target_link_options(hftrec_corpus_contract PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
elseif(CXET_SANITIZER_PROFILE STREQUAL "thread")
    target_compile_options(hftrec_corpus_contract PRIVATE
        -O2 -g3 -fsanitize=thread -fno-omit-frame-pointer
        -fno-optimize-sibling-calls)
    target_link_options(hftrec_corpus_contract PRIVATE
        -fsanitize=thread -fno-omit-frame-pointer)
endif()
