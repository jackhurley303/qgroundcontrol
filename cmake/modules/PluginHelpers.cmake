# ============================================================================
# QGroundControl Plugin Helper Functions
# ============================================================================

#[=======================================================================[.rst:
qgc_add_plugin
--------------

Helper function to configure a QGroundControl plugin with standard settings.

This function automatically applies common compile definitions and include
directories that plugins need to interface with QGC APIs.

Example usage:
  qgc_add_plugin(MyPlugin
      SOURCES
          MyPlugin.h
          MyPlugin.cc
      QRC_FILES
          MyPlugin.qrc
  )

#]=======================================================================]

function(qgc_add_plugin PLUGIN_NAME)
    set(options "")
    set(oneValueArgs QRC_FILES)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(PLUGIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    # Create the plugin as a MODULE library
    add_library(${PLUGIN_NAME} MODULE ${PLUGIN_SOURCES})
    
    # Enable Qt features
    set_target_properties(${PLUGIN_NAME} PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
    )
    
    # Apply common compile definitions from main QGC build
    if(DEFINED QGC_PLUGIN_COMPILE_DEFINITIONS)
        target_compile_definitions(${PLUGIN_NAME}
            PRIVATE
                ${QGC_PLUGIN_COMPILE_DEFINITIONS}
        )
    endif()
    
    # Apply common include directories from main QGC build
    if(DEFINED QGC_PLUGIN_INCLUDE_DIRECTORIES)
        target_include_directories(${PLUGIN_NAME}
            PRIVATE
                ${QGC_PLUGIN_INCLUDE_DIRECTORIES}
        )
    endif()
    
    # Ensure mavlink-generated headers exist before this plugin compiles.
    # The mavlink CPM target generates headers at build time; without this dependency
    # a parallel build can compile the plugin before the headers are ready.
    if(TARGET mavlink)
        add_dependencies(${PLUGIN_NAME} mavlink)
    endif()

    # Link against required Qt libraries
    target_link_libraries(${PLUGIN_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Qml
            Qt6::Quick
            Qt6::Widgets
            Qt6::Network
    )
    
    # Platform-specific plugin extension
    if(WIN32)
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".dll")
    elseif(APPLE)
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".dylib")
    else()
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".so")
    endif()
    
    message(STATUS "  Configured plugin: ${PLUGIN_NAME}")
endfunction()
