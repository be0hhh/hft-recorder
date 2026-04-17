include(GoogleTest)

add_executable(hftrec_unit_tests
    test_smoke.cpp
)

target_include_directories(hftrec_unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hftrec_unit_tests
    PRIVATE
        hftrec_core
        GTest::gtest
        GTest::gtest_main
)

target_compile_features(hftrec_unit_tests PRIVATE cxx_std_23)

gtest_discover_tests(hftrec_unit_tests)
