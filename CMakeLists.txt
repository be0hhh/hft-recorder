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
set(CXET_REPLAY_SHARED_LIB "" CACHE FILEPATH "Path to prebuilt libcxet_replay_core shared library")
set(HFTREC_CXET_MODE "AUTO" CACHE STRING "CXET integration mode: AUTO, ON, or OFF")
set_property(CACHE HFTREC_CXET_MODE PROPERTY STRINGS AUTO ON OFF)
option(HFTREC_HOTPATH_METRICS_DEFAULT "Enable hft-recorder/CXET hot-path metrics by default" ON)
if(HFTREC_HOTPATH_METRICS_DEFAULT)
    set(HFTREC_HOTPATH_METRICS_DEFAULT_VALUE 1)
else()
    set(HFTREC_HOTPATH_METRICS_DEFAULT_VALUE 0)
endif()
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
    get_filename_component(_cxet_lib_dir "${CXET_SHARED_LIB}" DIRECTORY)
    add_library(cxet_lib SHARED IMPORTED GLOBAL)
    set_target_properties(cxet_lib PROPERTIES
        IMPORTED_LOCATION "${CXET_SHARED_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CXET_PUBLIC_INCLUDE_DIR}"
        INTERFACE_LINK_DIRECTORIES "${_cxet_lib_dir}"
        INTERFACE_LINK_OPTIONS "$<$<PLATFORM_ID:Linux>:-Wl,-rpath-link,${_cxet_lib_dir}>;$<$<PLATFORM_ID:Linux>:-Wl,-rpath,${_cxet_lib_dir}>"
    )
endif()

set(HFTREC_WITH_CXET_REPLAY OFF)
set(_CXET_REPLAY_INCLUDE_DIR "")
if(CXET_PUBLIC_INCLUDE_DIR)
    get_filename_component(_CXET_PUBLIC_INCLUDE_PARENT "${CXET_PUBLIC_INCLUDE_DIR}" DIRECTORY)
    if(EXISTS "${_CXET_PUBLIC_INCLUDE_PARENT}/cxet/replay/ReplaySession.hpp")
        set(_CXET_REPLAY_INCLUDE_DIR "${_CXET_PUBLIC_INCLUDE_PARENT}")
    elseif(EXISTS "${CXET_PUBLIC_INCLUDE_DIR}/cxet/replay/ReplaySession.hpp")
        set(_CXET_REPLAY_INCLUDE_DIR "${CXET_PUBLIC_INCLUDE_DIR}")
    endif()
endif()
if(HFTREC_WITH_CXET AND CXET_REPLAY_SHARED_LIB
        AND _CXET_REPLAY_INCLUDE_DIR
        AND EXISTS "${CXET_REPLAY_SHARED_LIB}")
    set(HFTREC_WITH_CXET_REPLAY ON)
    add_library(cxet_replay_core SHARED IMPORTED GLOBAL)
    set_target_properties(cxet_replay_core PROPERTIES
        IMPORTED_LOCATION "${CXET_REPLAY_SHARED_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${_CXET_REPLAY_INCLUDE_DIR}"
    )
    message(STATUS "hft-recorder: CXET replay-core integration enabled")
else()
    message(STATUS "hft-recorder: CXET replay-core integration not found; recorded viewer uses offline fallback")
endif()

find_package(Threads REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(Qt6 COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 Network QUIET)

set(HFT_COMPRESSOR_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../hft-compressor/include" CACHE PATH "Path to hft-compressor public headers")
set(HFT_COMPRESSOR_SHARED_LIB "" CACHE FILEPATH "Path to prebuilt hft-compressor shared/import library")
set(HFT_COMPRESSOR_RUNTIME_LIB "" CACHE FILEPATH "Path to hft-compressor runtime library, usually DLL on Windows")
if(NOT HFT_COMPRESSOR_SHARED_LIB)
    set(_HFT_COMPRESSOR_LIB_CANDIDATES
        "${CMAKE_CURRENT_SOURCE_DIR}/../hft-compressor/build/libhft_compressor_core.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/../hft-compressor/build/lib/libhft_compressor_core.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/../hft-compressor/build/Release/hft_compressor_core.lib"
    )
    foreach(_candidate IN LISTS _HFT_COMPRESSOR_LIB_CANDIDATES)
        if(EXISTS "${_candidate}")
            set(HFT_COMPRESSOR_SHARED_LIB "${_candidate}" CACHE FILEPATH "Path to prebuilt hft-compressor shared/import library" FORCE)
            break()
        endif()
    endforeach()
endif()
if(NOT HFT_COMPRESSOR_RUNTIME_LIB AND HFT_COMPRESSOR_SHARED_LIB)
    get_filename_component(_hft_compressor_lib_dir "${HFT_COMPRESSOR_SHARED_LIB}" DIRECTORY)
    if(EXISTS "${_hft_compressor_lib_dir}/hft_compressor_core.dll")
        set(HFT_COMPRESSOR_RUNTIME_LIB "${_hft_compressor_lib_dir}/hft_compressor_core.dll" CACHE FILEPATH "Path to hft-compressor runtime library, usually DLL on Windows" FORCE)
    endif()
endif()

if(HFT_COMPRESSOR_PUBLIC_INCLUDE_DIR AND HFT_COMPRESSOR_SHARED_LIB
        AND EXISTS "${HFT_COMPRESSOR_PUBLIC_INCLUDE_DIR}/hft_compressor/compressor.hpp"
        AND EXISTS "${HFT_COMPRESSOR_SHARED_LIB}")
    get_filename_component(_hft_compressor_lib_ext "${HFT_COMPRESSOR_SHARED_LIB}" EXT)
    if(WIN32 AND _hft_compressor_lib_ext STREQUAL ".lib")
        add_library(hft_compressor_core SHARED IMPORTED GLOBAL)
        set_target_properties(hft_compressor_core PROPERTIES
            IMPORTED_IMPLIB "${HFT_COMPRESSOR_SHARED_LIB}"
            IMPORTED_LOCATION "${HFT_COMPRESSOR_RUNTIME_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${HFT_COMPRESSOR_PUBLIC_INCLUDE_DIR}"
            INTERFACE_COMPILE_DEFINITIONS HFT_COMPRESSOR_SHARED
        )
    else()
        add_library(hft_compressor_core UNKNOWN IMPORTED GLOBAL)
        set_target_properties(hft_compressor_core PROPERTIES
            IMPORTED_LOCATION "${HFT_COMPRESSOR_SHARED_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${HFT_COMPRESSOR_PUBLIC_INCLUDE_DIR}"
            INTERFACE_COMPILE_DEFINITIONS HFT_COMPRESSOR_SHARED
        )
    endif()
    message(STATUS "hft-recorder: hft-compressor prebuilt dependency enabled: ${HFT_COMPRESSOR_SHARED_LIB}")
else()
    message(FATAL_ERROR "hft-recorder compression tab requires prebuilt hft-compressor. Configure with HFT_COMPRESSOR_PUBLIC_INCLUDE_DIR and HFT_COMPRESSOR_SHARED_LIB.")
endif()

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
