# src/ — top-level tree for hft-recorder.
# Layout per doc/SOURCE_LAYOUT_AND_VARIANTS.md.

add_subdirectory(core)

if(HFTREC_BUILD_APP_TARGETS OR HFTREC_BUILD_GUI OR HFTREC_BUILD_TESTS)
    add_subdirectory(support)
    add_subdirectory(variants)
endif()

if(HFTREC_BUILD_APP_TARGETS)
    add_subdirectory(app)
endif()

if(HFTREC_BUILD_GUI)
    add_subdirectory(gui)
endif()
