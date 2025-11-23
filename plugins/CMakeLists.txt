# ============================================================================
# QGroundControl Plugins Build Configuration
# ============================================================================

message(STATUS "QGC: Configuring plugins")

# Automatically discover and build all plugin subdirectories
file(GLOB PLUGIN_DIRS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(PLUGIN_DIR ${PLUGIN_DIRS})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_DIR}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_DIR}/CMakeLists.txt")
            message(STATUS "QGC: Building plugin: ${PLUGIN_DIR}")
            add_subdirectory(${PLUGIN_DIR})
        endif()
    endif()
endforeach()
