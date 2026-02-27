# ============================================================================
# MAVLink Module
# MAVLink protocol implementation and communication handling
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        ImageProtocolManager.cc
        ImageProtocolManager.h
        MAVLinkFTP.cc
        MAVLinkFTP.h
        MAVLinkLib.h
        MAVLinkSigning.cc
        MAVLinkSigning.h
        MAVLinkStreamConfig.cc
        MAVLinkStreamConfig.h
        QGCMAVLink.cc
        QGCMAVLink.h
        StatusTextHandler.cc
        StatusTextHandler.h
        SysStatusSensorInfo.cc
        SysStatusSensorInfo.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# MAVLink Protocol Library Integration
# ============================================================================

message(STATUS "QGC: Building MAVLink")

CPMAddPackage(
    NAME mavlink
    GIT_REPOSITORY ${QGC_MAVLINK_GIT_REPO}
    GIT_TAG ${QGC_MAVLINK_GIT_TAG}
    OPTIONS
        "MAVLINK_DIALECT ${QGC_MAVLINK_DIALECT}"
        "MAVLINK_VERSION ${QGC_MAVLINK_VERSION}"
)

if(mavlink_ADDED)
    add_dependencies(${CMAKE_PROJECT_NAME} mavlink)
endif()

if(DEFINED mavlink_BINARY_DIR)
    target_include_directories(${CMAKE_PROJECT_NAME}
        PRIVATE
            ${mavlink_BINARY_DIR}/include/mavlink
            ${mavlink_BINARY_DIR}/include/mavlink/${QGC_MAVLINK_DIALECT}
    )
    # Export as CACHE INTERNAL so the root CMakeLists.txt can add these paths to
    # QGC_PLUGIN_INCLUDE_DIRECTORIES for plugin builds.  mavlink_BINARY_DIR is only
    # in scope here (CPMAddPackage PARENT_SCOPE doesn't reach the root directory).
    set(QGC_MAVLINK_INCLUDE_DIRS
        ${mavlink_BINARY_DIR}/include/mavlink
        ${mavlink_BINARY_DIR}/include/mavlink/${QGC_MAVLINK_DIALECT}
        CACHE INTERNAL "MAVLink generated-header include directories"
    )
endif()

# ----------------------------------------------------------------------------
# LibEvents - MAVLink event handling
# ----------------------------------------------------------------------------
add_subdirectory(LibEvents)
