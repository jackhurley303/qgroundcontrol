# ============================================================================
# PluginSystem Unit Tests
# Tests for plugin manifest/contributions parsing and host-compatibility validation
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        HostServicesTest.cc
        HostServicesTest.h
        HostVehicleServicesTest.cc
        HostVehicleServicesTest.h
        PluginContributionsTest.cc
        PluginContributionsTest.h
        PluginManifestTest.cc
        PluginManifestTest.h
        QGCPluginManagerTest.cc
        QGCPluginManagerTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
