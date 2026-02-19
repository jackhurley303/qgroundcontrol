# ============================================================================
# QGroundControl Plugins Build Configuration
# ============================================================================

message(STATUS "QGC: Configuring plugins")

# Include plugin helper functions
include(${CMAKE_SOURCE_DIR}/cmake/modules/PluginHelpers.cmake)

# Automatically discover and build all plugin subdirectories
# CONFIGURE_DEPENDS ensures CMake reconfigures when plugin directories are added/removed
file(GLOB PLUGIN_DIRS CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(PLUGIN_DIR ${PLUGIN_DIRS})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_DIR}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_DIR}/CMakeLists.txt")
            message(STATUS "QGC: Building plugin: ${PLUGIN_DIR}")
            add_subdirectory(${PLUGIN_DIR})
        endif()
    endif()
endforeach()
