cmake_minimum_required(VERSION 3.18)
project(artnode-rpi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ENABLE_HUB75    "Enable HUB75 matrix panel via rpi-rgb-led-matrix" OFF)
option(MOCK_HARDWARE   "Stub out RPi hardware layers for local/CI builds"  OFF)

# Auto-enable mock mode when not on Linux (e.g. macOS development machine)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Non-Linux host detected — enabling MOCK_HARDWARE automatically")
    set(MOCK_HARDWARE ON CACHE BOOL "" FORCE)
endif()

if(MOCK_HARDWARE AND ENABLE_HUB75)
    message(WARNING "MOCK_HARDWARE and ENABLE_HUB75 are both ON; disabling HUB75 for mock build")
    set(ENABLE_HUB75 OFF CACHE BOOL "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)

# nlohmann/json — config file serialization
#
# We download the include.zip release asset instead of the git repo for two reasons:
#   1. v3.11.x introduced an internal struct layout change (m_data union) that
#      fails to compile on GCC 10 / Raspbian Bullseye and some GCC 12 ARM builds.
#   2. v3.10.5's CMakeLists.txt uses cmake_minimum_required(VERSION 3.1), which
#      CMake 3.27+ rejects — bypassed entirely by fetching the header archive.
#
# include.zip contains single_include/nlohmann/json.hpp — we create the
# interface target ourselves; no library CMakeLists.txt is ever processed.
FetchContent_Declare(
    nlohmann_json_hdr
    URL https://github.com/nlohmann/json/releases/download/v3.10.5/include.zip
    URL_HASH SHA256=b94997df68856753b72f0d7a3703b7d484d4745c567f3584ef97c96c25a5798e
)
FetchContent_GetProperties(nlohmann_json_hdr)
if(NOT nlohmann_json_hdr_POPULATED)
    FetchContent_Populate(nlohmann_json_hdr)
endif()
add_library(nlohmann_json_iface INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_iface)
target_include_directories(nlohmann_json_iface INTERFACE
    ${nlohmann_json_hdr_SOURCE_DIR}/single_include
)

# cpp-httplib — embedded HTTP server (header-only, no OpenSSL needed)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
    GIT_SHALLOW    TRUE
)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# rpi_ws281x — only needed for real hardware builds
if(NOT MOCK_HARDWARE)
    # Requires running as root (or with CAP_SYS_RAWIO + /dev/mem access).
    FetchContent_Declare(
        ws2811
        GIT_REPOSITORY https://github.com/jgarff/rpi_ws281x.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ws2811)
endif()

# ---------------------------------------------------------------------------
# HUB75 (optional, real hardware only)
# ---------------------------------------------------------------------------
if(ENABLE_HUB75)
    FetchContent_Declare(
        rgbmatrix
        GIT_REPOSITORY https://github.com/hzeller/rpi-rgb-led-matrix.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(rgbmatrix)
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/runtime_config.cpp
    src/pattern_engine.cpp
    src/led_controller.cpp
    src/artnet.cpp
    src/web_config.cpp
)

if(ENABLE_HUB75)
    list(APPEND SOURCES src/hub75_controller.cpp)
endif()

add_executable(artnode-rpi ${SOURCES})

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
target_include_directories(artnode-rpi PRIVATE
    src
    ${ws2811_SOURCE_DIR}
)

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(artnode-rpi PRIVATE
    nlohmann_json::nlohmann_json
    httplib::httplib
    pthread
    m
)

if(MOCK_HARDWARE)
    target_compile_definitions(artnode-rpi PRIVATE
        MOCK_HARDWARE=1
        ARTNODE_CONFIG_PATH="/tmp/artnode/config.json"
    )
    # No ws2811 include path needed
else()
    target_include_directories(artnode-rpi PRIVATE ${ws2811_SOURCE_DIR})
    target_link_libraries(artnode-rpi PRIVATE ws2811)
    target_compile_definitions(artnode-rpi PRIVATE
        ARTNODE_CONFIG_PATH="/etc/artnode/config.json"
    )
endif()

if(ENABLE_HUB75)
    target_include_directories(artnode-rpi PRIVATE
        ${rgbmatrix_SOURCE_DIR}/include
    )
    target_link_libraries(artnode-rpi PRIVATE
        rpi-rgb-led-matrix
    )
    target_compile_definitions(artnode-rpi PRIVATE ENABLE_HUB75=1)
endif()

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
target_compile_options(artnode-rpi PRIVATE
    -Wall -Wextra -Wpedantic
    -O2
    # Suppress overly verbose warnings from third-party headers
    -Wno-unused-parameter
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS artnode-rpi DESTINATION /usr/local/bin)
install(FILES install/artnode-rpi.service DESTINATION /etc/systemd/system)

# ---------------------------------------------------------------------------
# Build summary
# ---------------------------------------------------------------------------
message(STATUS "artnode-rpi build options:")
message(STATUS "  MOCK_HARDWARE : ${MOCK_HARDWARE}")
message(STATUS "  HUB75 support : ${ENABLE_HUB75}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Build:   cmake --build build")
message(STATUS "Install: sudo cmake --install build")
message(STATUS "")
message(STATUS "NOTE: the binary must run as root (or with rawio capability)")
message(STATUS "      for rpi-ws281x PWM/DMA access to the LED strips.")
