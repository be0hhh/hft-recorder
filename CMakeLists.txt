cmake_minimum_required(VERSION 3.20)
project(hft-recorder LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# hft-recorder is a standalone consumer of a prebuilt CXETCPP library when
# available. Offline viewer builds must not build or vendor parent sources.
set(CXET_PUBLIC_INCLUDE_DIR "" CACHE PATH "Path to CXETCPP public headers")
set(CXET_SHARED_LIB "" CACHE FILEPATH "Path to prebuilt libcxet_lib shared library")
set(HFTREC_CXET_MODE "AUTO" CACHE STRING "CXET integration mode: AUTO, ON, or OFF")
set_property(CACHE HFTREC_CXET_MODE PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${HFTREC_CXET_MODE}" HFTREC_CXET_MODE_NORMALIZED)

set(HFTREC_WITH_CXET OFF)
if(HFTREC_CXET_MODE_NORMALIZED STREQUAL "OFF")
    message(STATUS "hft-recorder: CXETCPP integration disabled")
elseif(CXET_PUBLIC_INCLUDE_DIR AND CXET_SHARED_LIB AND EXISTS "${CXET_PUBLIC_INCLUDE_DIR}/cxet.hpp" AND EXISTS "${CXET_SHARED_LIB}")
    set(HFTREC_WITH_CXET ON)
    message(STATUS "hft-recorder: CXETCPP integration enabled")
else()
    if(HFTREC_CXET_MODE_NORMALIZED STREQUAL "ON")
        message(FATAL_ERROR "HFTREC_CXET_MODE=ON requires valid CXET_PUBLIC_INCLUDE_DIR and CXET_SHARED_LIB")
    endif()
    message(STATUS "hft-recorder: CXETCPP integration not found; building offline viewer mode")
endif()

if(HFTREC_WITH_CXET)
    add_library(cxet_lib SHARED IMPORTED GLOBAL)
    set_target_properties(cxet_lib PROPERTIES
        IMPORTED_LOCATION "${CXET_SHARED_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CXET_PUBLIC_INCLUDE_DIR}"
    )
endif()

find_package(Threads REQUIRED)
find_package(Qt6 COMPONENTS Core Gui Qml Quick QuickControls2 Network QUIET)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

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