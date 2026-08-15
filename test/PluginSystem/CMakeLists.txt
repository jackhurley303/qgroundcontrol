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
        PluginInstallerTest.cc
        PluginInstallerTest.h
        PluginLoaderGateTest.cc
        PluginLoaderGateTest.h
        PluginManifestTest.cc
        PluginManifestTest.h
        PluginRecordStoreTest.cc
        PluginRecordStoreTest.h
        PluginTeardownTest.cc
        PluginTeardownTest.h
        PluginTrustGateTest.cc
        PluginTrustGateTest.h
        PluginUIModuleTest.cc
        PluginUIModuleTest.h
        QGCPluginManagerTest.cc
        QGCPluginManagerTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Compiled into the app's own qrc:/qml tree, so it exists before any QML engine
# does — the host half of the late-activation cache test (see the file's comment).
qt_add_resources(${CMAKE_PROJECT_NAME} "PluginSystemTest_qml"
    PREFIX "/qml"
    FILES
        PluginActivationHostProbe.qml
)

# Real SDK-tier dylib fixtures backing PluginLoaderGateTest, built through the
# same qgc_add_plugin() every real plugin uses (see TestPlugin/CMakeLists.txt).
add_subdirectory(TestPlugin)
add_dependencies(${CMAKE_PROJECT_NAME} TestPluginFixture TestPluginFixtureV2)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    QGC_TEST_PLUGIN_FIXTURE_PATH="$<TARGET_FILE:TestPluginFixture>"
    QGC_TEST_PLUGIN_FIXTURE_V2_PATH="$<TARGET_FILE:TestPluginFixtureV2>"
)

# The reference plugin itself, built by plugins/CMakeLists.txt (added before this
# directory). PluginTeardownTest drives its real dylib through an activate →
# deactivate → reactivate cycle: the lifecycle contract SDK-README.md documents is
# only worth documenting if the shipped example provably satisfies it.
#
# Guarded because the root CMakeLists.txt treats the whole plugins/ tree as optional;
# a tree without it builds and PluginTeardownTest skips. The elseif is the loud half:
# plugins/example present but no target means the plugin failed to configure, and the
# test would otherwise skip on what is really a build failure.
if(TARGET ExamplePlugin)
    add_dependencies(${CMAKE_PROJECT_NAME} ExamplePlugin)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        QGC_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:ExamplePlugin>"
    )
elseif(EXISTS "${CMAKE_SOURCE_DIR}/plugins/example/CMakeLists.txt")
    message(FATAL_ERROR
        "plugins/example exists but target ExamplePlugin was never defined; "
        "PluginTeardownTest cannot verify the plugin lifecycle contract without it")
endif()

add_qgc_test(HostServicesTest LABELS Unit)
add_qgc_test(HostVehicleServicesTest LABELS Integration Vehicle RESOURCE_LOCK MockLink)
add_qgc_test(PluginContributionsTest LABELS Unit)
add_qgc_test(PluginInstallerTest LABELS Unit)
add_qgc_test(PluginLoaderGateTest LABELS Unit)
add_qgc_test(PluginManifestTest LABELS Unit)
add_qgc_test(PluginRecordStoreTest LABELS Unit)
add_qgc_test(PluginTeardownTest LABELS Unit)
add_qgc_test(PluginTrustGateTest LABELS Unit)
add_qgc_test(PluginUIModuleTest LABELS Unit)
add_qgc_test(QGCPluginManagerTest LABELS Unit)
