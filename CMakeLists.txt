cmake_minimum_required(VERSION 3.20)
project(hft-recorder LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# hft-recorder is a standalone consumer of a prebuilt CXETCPP library.
# It must not build or vendor the parent project sources.
# See doc/BUILD_AND_ISOLATION.md.
#
# Inputs from the parent workspace (set by run.sh or -D flags):
#   CXET_PUBLIC_INCLUDE_DIR — directory containing public CXETCPP headers
#                             (the directory that holds cxet.hpp)
#   CXET_SHARED_LIB         — path to prebuilt libcxet_lib.so
set(CXET_PUBLIC_INCLUDE_DIR "" CACHE PATH "Path to CXETCPP public headers")
set(CXET_SHARED_LIB "" CACHE FILEPATH "Path to prebuilt libcxet_lib shared library")

if(NOT CXET_PUBLIC_INCLUDE_DIR OR NOT CXET_SHARED_LIB)
    message(WARNING
        "CXET_PUBLIC_INCLUDE_DIR or CXET_SHARED_LIB not set — "
        "use run.sh to build, or pass them with -D.")
endif()

add_library(cxet_lib SHARED IMPORTED GLOBAL)
set_target_properties(cxet_lib PROPERTIES
    IMPORTED_LOCATION "${CXET_SHARED_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${CXET_PUBLIC_INCLUDE_DIR}"
)

find_package(Threads REQUIRED)
find_package(Qt6 COMPONENTS Core Gui Qml Quick QuickControls2 QUIET)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# spdlog + fmt — logging per doc/LOGGING_AND_METRICS.md.
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.0.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)

set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# GoogleTest — unit/integration tests per doc/TESTING_CONTRACT.md.
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -Wno-unused-variable
)
add_compile_options($<$<CONFIG:Release>:-O2>)
add_compile_options($<$<CONFIG:Release>:-g>)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(src)
add_subdirectory(tests)
