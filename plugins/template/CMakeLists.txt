cmake_minimum_required(VERSION 3.25)
project(MyPlugin VERSION 1.0.0 LANGUAGES CXX)

# plugins/CMakeLists.txt auto-discovers every directory under plugins/ with a
# CMakeLists.txt as an in-tree plugin (add_subdirectory). This directory is not
# that — it's the out-of-tree SDK template, meant to be built standalone against an
# *installed* SDK package (find_package(QGCPluginAPI) below has nothing to find
# in-tree). Bail out when swept up by that glob; PROJECT_IS_TOP_LEVEL is false only
# in that case, never for a real standalone build of this directory.
if(NOT PROJECT_IS_TOP_LEVEL)
    message(STATUS "QGC: Skipping plugins/template (packaged SDK's out-of-tree template, not an in-tree plugin — see SDK-README.md)")
    return()
endif()

# Copy-and-build starting point for a QGroundControl Tier B (SDK) plugin.
# This directory never needs a QGroundControl checkout — it builds standalone
# against the published QGCPluginAPI SDK package. See ../SDK-README.md for
# the compatibility contract this depends on.
#
#   cmake -B build -DCMAKE_PREFIX_PATH=/path/to/qgc-plugin-sdk-macos-<version>
#   cmake --build build
#
# Then copy the built library into QGC's user plugins directory (see
# SDK-README.md's "Try it" section) and relaunch QGC.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Major version pin: rejects an SDK whose major doesn't match what this plugin
# was written against (D3's SOVERSION rule, surfaced through find_package()).
find_package(QGCPluginAPI 2 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(MyPlugin MODULE
    MyPlugin.h
    MyPlugin.cc
    MyPlugin.qrc
)

# QGCPluginAPI_VERSION_MAJOR comes from find_package() above (QGCPluginAPIConfigVersion.cmake) —
# reading it here instead of hardcoding keeps the manifest's declared apiVersion tied to
# the SDK actually being linked, not a value that can silently drift from it.
configure_file(qgcplugin.json.in "${CMAKE_CURRENT_BINARY_DIR}/qgcplugin.json" @ONLY)
target_include_directories(MyPlugin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(MyPlugin PRIVATE QGCPluginAPI::QGCPluginAPI)

# No -undefined dynamic_lookup / -Wl,--allow-shlib-undefined here: every symbol this
# plugin needs resolves from the linked QGCPluginAPI dylib (D2/D3), the SDK-tier shape
# plugins/example/CMakeLists.txt uses in-tree.
if(APPLE)
    set_target_properties(MyPlugin PROPERTIES SUFFIX ".dylib")
elseif(WIN32)
    set_target_properties(MyPlugin PROPERTIES SUFFIX ".dll")
else()
    set_target_properties(MyPlugin PROPERTIES SUFFIX ".so")
endif()
