# src/ — top-level tree for hft-recorder.
# Layout per docs/SOURCE_LAYOUT_AND_VARIANTS.md.

add_subdirectory(Runtime)

if(HFTREC_BUILD_APP_TARGETS OR HFTREC_BUILD_GUI OR HFTREC_BUILD_TESTS)
    add_subdirectory(Lab/Codecs)
    add_subdirectory(Lab/Variants)
endif()

if(HFTREC_BUILD_APP_TARGETS)
    add_subdirectory(Process)
endif()

if(HFTREC_BUILD_GUI)
    add_subdirectory(Gui)
endif()
