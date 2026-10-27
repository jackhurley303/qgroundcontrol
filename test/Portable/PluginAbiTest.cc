#include "PluginAbiTest.h"

#include <QtCore/QFile>

#include "PluginManifest.h"
#include "QGCPlugin.h"
#include "QGCPluginLoader.h"

// Exercises both vtables a plugin binary carries: QGCPluginInterface's
// (pluginInterfaceVersion() and createPlugin(), called by activate()) and QGCPlugin's own
// (replayExtension(), called below).
void PluginAbiTest::_inspectAndActivateTemplatePlugin_test()
{
    const QString path = qEnvironmentVariable("QGC_TEMPLATE_PLUGIN_PATH");
    if (path.isEmpty()) {
        QSKIP("QGC_TEMPLATE_PLUGIN_PATH not set; skipping the SDK-template plugin ABI check (set by CI)");
    }
    QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("SDK-template plugin does not exist: %1").arg(path)));

    PluginLoadInfo info = QGCPluginLoader::inspect(path);
    QVERIFY2(info.state == PluginState::Discovered, qPrintable(info.errorString));
    QCOMPARE(info.manifest.tier, PluginManifest::Tier::Sdk);

    QGCPluginLoader::activate(info);
    QVERIFY2(info.state == PluginState::Active, qPrintable(info.errorString));
    QVERIFY(info.plugin != nullptr);
    QCOMPARE(info.plugin->replayExtension(), nullptr);
    delete info.plugin;
}

QGC_REGISTER_PORTABLE_TEST(PluginAbiTest, TestLabel::Unit)
