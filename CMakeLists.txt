cmake_minimum_required(VERSION 3.20)
project(hft-recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# hft-recorder is a standalone consumer of a prebuilt CXETCPP library.
# It must not build or vendor the parent project sources.
#
# Expected inputs from the parent workspace:
# - CXET_PUBLIC_INCLUDE_DIR: directory containing the public CXETCPP headers
# - CXET_SHARED_LIB: path to the prebuilt shared library, e.g. ../build/libcxet_lib.so

set(CXET_PUBLIC_INCLUDE_DIR "" CACHE PATH "Path to CXETCPP public headers")
set(CXET_SHARED_LIB "" CACHE FILEPATH "Path to prebuilt libcxet_lib shared library")

add_library(cxet_lib SHARED IMPORTED GLOBAL)
set_target_properties(cxet_lib PROPERTIES
    IMPORTED_LOCATION "${CXET_SHARED_LIB}"
)

add_executable(hft-recorder
    src/main.cpp
)

target_include_directories(hft-recorder PRIVATE
    ${CXET_PUBLIC_INCLUDE_DIR}
    include
    src
)

target_link_libraries(hft-recorder PRIVATE
    cxet_lib
)

# Runtime lookup policy is intentionally left external for now.
# Use either RPATH configuration or LD_LIBRARY_PATH when the executable is introduced.
