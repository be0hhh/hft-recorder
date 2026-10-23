# Recorder-owned canonical corpus contract shared with offline consumers.
# Keep this target independent from Qt, capture runtimes, and trading runtime state.
find_package(Threads REQUIRED)

add_library(hftrec_corpus_contract STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Capture/Session/SessionManifest.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Corpus/Codec/Crc32c.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Corpus/BinaryMarketCorpusReader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Corpus/BinaryMarketCorpusWriter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Corpus/CorpusCut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Capture/Parser/BufferedCorpusWriter.cpp
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
target_link_libraries(hftrec_corpus_contract PRIVATE hft_compressor::core Threads::Threads)

# Producer-owned local client: no Qt, capture owner, credential or Parser
# runtime dependency is allowed in a research frontend's link closure.
add_library(hftrec_service_client STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../Service/src/RecorderServiceClient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Service/src/RecorderServiceConfig.cpp)
add_library(hftrec::service_client ALIAS hftrec_service_client)
target_include_directories(hftrec_service_client PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../Service/include)
target_compile_features(hftrec_service_client PUBLIC cxx_std_23)
target_link_libraries(hftrec_service_client PUBLIC hftrec::corpus_contract cxet::read_only_contracts)
set_target_properties(hftrec_service_client PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(hftrec_reference_contract INTERFACE)
add_library(hftrec::reference_contract ALIAS hftrec_reference_contract)
target_link_libraries(hftrec_reference_contract INTERFACE hftrec::corpus_contract hftp_contract_headers)

# Secret-free Recorder launch selection is shared by the GUI/CLI and its
# offline acceptance tests, without pulling in live Parser process ownership.
add_library(hftrec_capture_launch STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Capture/Coordinator/RecorderCaptureSelection.cpp)
add_library(hftrec::capture_launch ALIAS hftrec_capture_launch)
target_include_directories(hftrec_capture_launch PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src)
target_compile_features(hftrec_capture_launch PUBLIC cxx_std_23)
set_target_properties(hftrec_capture_launch PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(TARGET hftp_contract_headers)
  add_library(hftrec_capture_client STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Capture/Parser/ParserMarketCaptureClient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src/Capture/Parser/Private/CorpusConversion.cpp)
  add_library(hftrec::capture_client ALIAS hftrec_capture_client)
  target_include_directories(hftrec_capture_client PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../Runtime/src)
  target_compile_features(hftrec_capture_client PUBLIC cxx_std_23)
  target_link_libraries(hftrec_capture_client PUBLIC hftrec::corpus_contract hftp_contract_headers)
  set_target_properties(hftrec_capture_client PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

set(_hftrec_offline_providers hftrec_corpus_contract hftrec_capture_launch)
if(TARGET hftrec_capture_client)
  list(APPEND _hftrec_offline_providers hftrec_capture_client)
endif()
foreach(_hftrec_offline_provider IN LISTS _hftrec_offline_providers)
if(CXET_SANITIZER_PROFILE STREQUAL "address-undefined")
    target_compile_options(${_hftrec_offline_provider} PRIVATE
        -O2 -g3 -fsanitize=address,undefined
        -fsanitize-address-use-after-scope -fno-omit-frame-pointer
        -fno-optimize-sibling-calls -fno-sanitize-recover=all)
    target_link_options(${_hftrec_offline_provider} PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
elseif(CXET_SANITIZER_PROFILE STREQUAL "thread")
    target_compile_options(${_hftrec_offline_provider} PRIVATE
        -O2 -g3 -fsanitize=thread -fno-omit-frame-pointer
        -fno-optimize-sibling-calls)
    target_link_options(${_hftrec_offline_provider} PRIVATE
        -fsanitize=thread -fno-omit-frame-pointer)
endif()
endforeach()
