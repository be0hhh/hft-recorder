# src/Lab/Codecs/ — thin wrappers around external codecs (linked in Phase 3+).
# Phase 1: header-only declarations; .cpp files return Status::Unimplemented.

add_library(hftrec_support STATIC
    src/ZstdWrapper.cpp
    src/Lz4Wrapper.cpp
    src/BrotliWrapper.cpp
    src/XzWrapper.cpp
)

target_include_directories(hftrec_support
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/Runtime/src
        ${PROJECT_SOURCE_DIR}/src/Lab/Codecs
)

target_link_libraries(hftrec_support
    PUBLIC
        hftrec_core
)

target_compile_features(hftrec_support PUBLIC cxx_std_23)
