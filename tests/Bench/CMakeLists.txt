add_executable(hftrec_recorder_memory_soak
    RecorderMemorySoak.cpp
    ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/LiveDataProvider.cpp
)

target_include_directories(hftrec_recorder_memory_soak
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/Runtime/include
        ${PROJECT_SOURCE_DIR}/src/Runtime/src
        ${PROJECT_SOURCE_DIR}/src/Gui/src
)

target_link_libraries(hftrec_recorder_memory_soak
    PRIVATE
        hftrec_core
        hftrec_support
)

target_compile_features(hftrec_recorder_memory_soak PRIVATE cxx_std_23)
