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
        PluginLoaderGateTest.cc
        PluginLoaderGateTest.h
        PluginManifestTest.cc
        PluginManifestTest.h
        QGCPluginManagerTest.cc
        QGCPluginManagerTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Real SDK-tier dylib fixture backing PluginLoaderGateTest (see TestPlugin/CMakeLists.txt
# for why it's a hand-rolled MODULE rather than a qgc_add_plugin() call).
add_subdirectory(TestPlugin)
add_dependencies(${CMAKE_PROJECT_NAME} TestPluginFixture)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    QGC_TEST_PLUGIN_FIXTURE_PATH="$<TARGET_FILE:TestPluginFixture>"
)
