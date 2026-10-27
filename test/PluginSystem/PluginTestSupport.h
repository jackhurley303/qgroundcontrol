#pragma once

#include <QtCore/QString>

#include "PluginSettings.h"
#include "SettingsManager.h"

// Helpers shared by the PluginSystem tests. Each test .cc used to define its own copy in an
// anonymous namespace, which redefines when a unity build merges the files into one unit.
namespace PluginTestSupport {

// Crash-sentinel keys, mirroring PluginRecordStore's constants
constexpr const char* kLoadingPluginIdKey = "PluginSystem/loadingPluginId";
constexpr const char* kCrashedPluginIdKey = "PluginSystem/crashedPluginId";

inline PluginSettings* pluginSettings()
{
    return SettingsManager::instance()->pluginSettings();
}

// Path to the real TestPluginFixture dylib, injected by
// test/PluginSystem/CMakeLists.txt as a compile definition. Its qrc registers
// TestPluginFixturePanel.qml into /qml when the library is loaded, which is the
// late registration _lateActivationResolvesPluginQml_test is about.
inline QString testPluginFixturePath()
{
    return QStringLiteral(QGC_TEST_PLUGIN_FIXTURE_PATH);
}

// Path to a second, independently-built dylib from the identical sources/manifest
// (same plugin id and version) — TestPlugin/CMakeLists.txt's TestPluginFixtureV2
// target, injected the same way as the primary fixture.
inline QString testPluginFixtureV2Path()
{
    return QStringLiteral(QGC_TEST_PLUGIN_FIXTURE_V2_PATH);
}

}  // namespace PluginTestSupport
