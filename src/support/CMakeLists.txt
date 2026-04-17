# src/support/ — thin wrappers around external codecs (linked in Phase 3+).
# Phase 1: header-only declarations; .cpp files return Status::Unimplemented.

add_library(hftrec_support STATIC
    external_wrappers/ZstdWrapper.cpp
    external_wrappers/Lz4Wrapper.cpp
    external_wrappers/BrotliWrapper.cpp
    external_wrappers/XzWrapper.cpp
)

target_include_directories(hftrec_support
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hftrec_support
    PUBLIC
        hftrec_core
)

target_compile_features(hftrec_support PUBLIC cxx_std_23)
