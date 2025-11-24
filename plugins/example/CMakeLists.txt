cmake_minimum_required(VERSION 3.25)

project(ExamplePlugin VERSION 1.0.0 LANGUAGES CXX)

# This plugin builds as part of the QGC build system
# It has access to all QGC headers and can link against QGC object files

# Plugin sources
add_library(ExamplePlugin MODULE
    ExamplePlugin.h
    ExamplePlugin.cc
    ExamplePlugin.qrc
)

# Link against Qt (already found by parent project)
target_link_libraries(ExamplePlugin
    PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::Widgets
)

# Note: We don't link against QGroundControlModule here.
# QGCCorePlugin symbols will be resolved at runtime from the main executable.
# The executable exports all symbols via -Wl,-export_dynamic flag.

# Include QGC headers (parent project already configured these)
target_include_directories(ExamplePlugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/API
        ${CMAKE_SOURCE_DIR}/src/UI
        ${CMAKE_SOURCE_DIR}/src/QmlControls
        ${CMAKE_SOURCE_DIR}/src/Settings
        ${CMAKE_SOURCE_DIR}/src/Utilities
        ${CMAKE_SOURCE_DIR}/src/Vehicle
        ${CMAKE_SOURCE_DIR}/src/FactSystem
)

# Set plugin properties
set_target_properties(ExamplePlugin PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
)

# Force platform-appropriate plugin extension
if(WIN32)
    set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".dll")
elseif(APPLE)
    set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".dylib")
else()
    set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".so")
endif()

# Allow undefined symbols - they'll be resolved from the main executable at runtime
if(APPLE)
    target_link_options(ExamplePlugin PRIVATE
        -undefined dynamic_lookup  # Resolve symbols from loading executable
    )
elseif(UNIX)
    # Linux: Allow undefined symbols to be resolved at runtime
    target_link_options(ExamplePlugin PRIVATE
        -Wl,--allow-shlib-undefined
    )
elseif(WIN32)
    # Windows: Plugins on Windows typically need an import library
    # This may require additional configuration depending on how QGC exports symbols
    message(STATUS "ExamplePlugin: Windows plugin loading may require additional configuration")
endif()

# Auto-deploy plugin to runtime location after build (for development)
if(APPLE)
    set(PLUGIN_DEPLOY_DIR "$ENV{HOME}/Library/Application Support/QGroundControl/QGroundControl Daily/plugins")
elseif(UNIX)
    set(PLUGIN_DEPLOY_DIR "$ENV{HOME}/.local/share/QGroundControl/QGroundControl Daily/plugins")
elseif(WIN32)
    set(PLUGIN_DEPLOY_DIR "$ENV{APPDATA}/QGroundControl/QGroundControl Daily/plugins")
endif()

add_custom_command(TARGET ExamplePlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ExamplePlugin>
        "${PLUGIN_DEPLOY_DIR}/"
    COMMENT "Deploying ExamplePlugin to ${PLUGIN_DEPLOY_DIR}"
)

message(STATUS "ExamplePlugin: Configured to build as QGC plugin")
message(STATUS "ExamplePlugin: Will auto-deploy to ${PLUGIN_DEPLOY_DIR}")
