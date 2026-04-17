add_library(hftrec_variant_orderbook_var01 STATIC
    encode.cpp
    decode.cpp
)

target_include_directories(hftrec_variant_orderbook_var01
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hftrec_variant_orderbook_var01
    PUBLIC
        hftrec_core
        hftrec_support
)

target_compile_features(hftrec_variant_orderbook_var01 PUBLIC cxx_std_23)
