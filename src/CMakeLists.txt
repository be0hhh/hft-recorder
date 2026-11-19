# src/ — top-level tree for hft-recorder.
# Layout per docs/SOURCE_LAYOUT_AND_VARIANTS.md.

add_subdirectory(Runtime)
add_subdirectory(Service)

if(HFTREC_BUILD_APP_TARGETS)
    add_subdirectory(Process)
endif()
