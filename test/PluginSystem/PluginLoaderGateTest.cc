#include "PluginLoaderGateTest.h"

#include "PluginManifest.h"
#include "QGCPlugin.h"
#include "QGCPluginInterface.h"
#include "QGCPluginLoader.h"

namespace {

// Path to the real TestPluginFixture dylib, injected by
// test/PluginSystem/CMakeLists.txt as a compile definition.
QString testPluginFixturePath()
{
    return QStringLiteral(QGC_TEST_PLUGIN_FIXTURE_PATH);
}

} // namespace

// Loads a real SDK-tier dylib through the actual QGCPluginLoader gate — the
// permanent regression harness for S3's "metadata before code, real activation"
// proof, in place of a hand-built PluginLoadInfo.

void PluginLoaderGateTest::_inspectRealPluginBeforeActivation_test()
{
    const PluginLoadInfo info = QGCPluginLoader::inspect(testPluginFixturePath());

    QCOMPARE(info.state, PluginState::Discovered);
    QCOMPARE(info.manifest.id, QStringLiteral("org.qgroundcontrol.test.pluginloadergate"));
    QCOMPARE(info.manifest.tier, PluginManifest::Tier::Sdk);
    QCOMPARE(info.manifest.apiVersion, QGCPluginApiVersion);
    QVERIFY(info.manifest.hostBuildId.isEmpty());

    // Contribution synthesis end-to-end: the manifest's declared toolMenu made
    // it through the real embedded metadata, not a hand-built QJsonObject.
    QCOMPARE(info.contributions.toolMenuItem[QStringLiteral("title")].toString(), QStringLiteral("Test Plugin Fixture"));
}

void PluginLoaderGateTest::_activateRealPlugin_test()
{
    PluginLoadInfo info = QGCPluginLoader::inspect(testPluginFixturePath());
    QCOMPARE(info.state, PluginState::Discovered);

    QGCPluginLoader::activate(info);

    QCOMPARE(info.state, PluginState::Active);
    QVERIFY(info.plugin != nullptr);
    QCOMPARE(info.plugin->replayExtension(), nullptr);

    delete info.plugin;
}

void PluginLoaderGateTest::_inspectMissingFileFails_test()
{
    const PluginLoadInfo info = QGCPluginLoader::inspect(QStringLiteral("/nonexistent/NoSuchPlugin.dylib"));

    QCOMPARE(info.state, PluginState::Failed);
    QVERIFY(!info.errorString.isEmpty());
}

UT_REGISTER_TEST(PluginLoaderGateTest, TestLabel::Unit)
