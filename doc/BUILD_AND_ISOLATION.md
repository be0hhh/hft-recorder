# hft-recorder - build and isolation

## Core rule

`hft-recorder` is a standalone application over `CXETCPP`.

It must consume the library as an already-built dependency:
- shared library: `libcxet_lib.so`
- public headers: only the API surface required by the recorder

It must not:
- compile the parent `CXETCPP` source tree
- call `add_subdirectory(..)` on the parent repo
- depend on `network/`, `parse/`, `exchanges/`, or runtime internals as implementation dependencies

## Allowed include surface

The intended include surface is the public recorder-facing API only, for example:

```cpp
#include "cxet.hpp"
#include "api/stream/CxetStream.hpp"
#include "primitives/composite/Trade.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/FundingRateInfo.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
```

Avoid includes that tie the recorder to internals, for example:

```cpp
#include "network/ws/WsClient.hpp"   // forbidden
#include "exchanges/binance/..."     // forbidden
#include "parse/..."                 // forbidden
#include "runtime/..."               // forbidden
```

## Current repo contract

`apps/hft-recorder/` is its own nested repo and owns:
- `doc/`
- `src/`
- `include/`
- `tests/`
- `bench/`
- `scripts/`

The parent `CXETCPP` repo owns:
- library implementation
- exchange integrations
- transport/runtime internals
- library build pipeline

## Baseline CMake shape

The recorder CMake should import a prebuilt shared library instead of rebuilding the parent project.

```cmake
cmake_minimum_required(VERSION 3.20)
project(hft-recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
```

## Runtime note

Executable runtime lookup for `libcxet_lib.so` is intentionally external at this stage.
Use either:
- `RPATH`
- `LD_LIBRARY_PATH`

when the real executable is introduced.

## Why this isolation matters

This keeps the recorder independent from:
- internal file moves inside `CXETCPP`
- parent compile graph churn
- accidental coupling to unfinished runtime/network internals

It also preserves the intended long-term model:
- `CXETCPP` evolves as a reusable library
- `hft-recorder` remains one client application on top of it
