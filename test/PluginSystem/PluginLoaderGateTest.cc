#include "PluginLoaderGateTest.h"

#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include "PluginManifest.h"
#include "QGCPlugin.h"
#include "QGCPluginInterface.h"
#include "QGCPluginLoader.h"
#include "qgc_version.h"

namespace {

// Path to the real TestPluginFixture dylib, injected by
// test/PluginSystem/CMakeLists.txt as a compile definition.
QString testPluginFixturePath()
{
    return QStringLiteral(QGC_TEST_PLUGIN_FIXTURE_PATH);
}

QJsonObject qmlPackageManifestJson(const QJsonObject& contributes = QJsonObject())
{
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("org.test.qmlpackage");
    json[QStringLiteral("name")] = QStringLiteral("QML Package");
    json[QStringLiteral("version")] = QStringLiteral("1.0.0");
    json[QStringLiteral("vendor")] = QStringLiteral("Test Org");
    json[QStringLiteral("description")] = QStringLiteral("Tier A package fixture");
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    hostVersion[QStringLiteral("max")] = QString();
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("contributes")] = contributes;
    return json;
}

QJsonObject sdkPackageManifestJson()
{
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("org.test.sdkpackage");
    json[QStringLiteral("name")] = QStringLiteral("SDK Package");
    json[QStringLiteral("version")] = QStringLiteral("1.0.0");
    json[QStringLiteral("vendor")] = QStringLiteral("Test Org");
    json[QStringLiteral("description")] = QStringLiteral("Tier B package fixture");
    json[QStringLiteral("tier")] = QStringLiteral("sdk");
    json[QStringLiteral("apiVersion")] = QGCPluginApiVersion;
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    hostVersion[QStringLiteral("max")] = QString();
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("contributes")] = QJsonObject();
    return json;
}

QJsonObject internalPackageManifestJson()
{
    QJsonObject json = sdkPackageManifestJson();
    json[QStringLiteral("id")] = QStringLiteral("org.test.internalpackage");
    json[QStringLiteral("tier")] = QStringLiteral("internal");
    json[QStringLiteral("hostBuildId")] = QStringLiteral(QGC_GIT_HASH);
    return json;
}

} // namespace

// Package directory fixture: writes qgcplugin.json at <tempDir>/<subdirName>/, mirroring
// D8's package layout. Returns the absolute package directory, or empty on failure.
QString PluginLoaderGateTest::_writePackage(const QString& subdirName, const QJsonObject& manifestJson)
{
    const QString packageDir = createSubDir(subdirName);
    if (packageDir.isEmpty()) {
        return QString();
    }
    const QJsonDocument doc(manifestJson);
    if (!createFile(subdirName + QStringLiteral("/qgcplugin.json"), doc.toJson())) {
        return QString();
    }
    return packageDir;
}

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

// The permanent "different-commit-host" ABI proof (04 §11 Spike S3, DoD #1's residue):
// a dylib built at a different commit (or, for the template leg, against a different
// SDK zip entirely) must still satisfy today's QGCPluginLoader gate. CI points these at
// a real path via env var; outside CI (or before U5.1's macos.yml wiring lands) the var
// is unset and the slot skips rather than failing.
//
// Exercises both compiled-in vtables a plugin binary carries: QGCPluginInterface's
// (pluginInterfaceVersion()/createPlugin(), invoked by activate() itself) and
// QGCPlugin's own (replayExtension(), invoked explicitly below, mirroring
// _activateRealPlugin_test's real-fixture check). A manual verify confirmed this
// actually catches a break: inserting a scratch virtual into QGCPluginInterface
// ahead of createPlugin() and reloading this committed golden dylib crashes with
// SIGSEGV (out-of-bounds vtable read against the old 2-slot layout) rather than
// silently misbehaving — reverted, not part of this commit.
void PluginLoaderGateTest::_inspectAndActivateExternalDylib(const QByteArray& envVarName, const QString& skipContext)
{
    const QString path = qEnvironmentVariable(envVarName.constData());
    if (path.isEmpty()) {
        QSKIP(qPrintable(QStringLiteral("%1 not set; skipping %2 ABI check (set by CI, see test/PluginSystem/golden/README.md)")
                          .arg(QString::fromUtf8(envVarName), skipContext)));
    }
    QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("%1 does not exist: %2").arg(skipContext, path)));

    PluginLoadInfo info = QGCPluginLoader::inspect(path);
    QCOMPARE(info.state, PluginState::Discovered);
    QCOMPARE(info.manifest.tier, PluginManifest::Tier::Sdk);

    QGCPluginLoader::activate(info);
    QCOMPARE(info.state, PluginState::Active);
    QVERIFY(info.plugin != nullptr);
    QCOMPARE(info.plugin->replayExtension(), nullptr);
    delete info.plugin;
}

void PluginLoaderGateTest::_activateGoldenPluginAgainstCurrentHost_test()
{
    _inspectAndActivateExternalDylib(QByteArrayLiteral("QGC_GOLDEN_PLUGIN_PATH"), QStringLiteral("golden plugin"));
}

void PluginLoaderGateTest::_activateTemplateBuiltPluginAgainstCurrentHost_test()
{
    _inspectAndActivateExternalDylib(QByteArrayLiteral("QGC_TEMPLATE_PLUGIN_PATH"), QStringLiteral("SDK-template plugin"));
}

// Package discovery + Tier A synthesis (U3.1) — recreates S4's manifest-only package
// recipe as a permanent fixture (the spike harness was throwaway and deleted).

void PluginLoaderGateTest::_inspectQmlPackageActivatesWithoutBinary_test()
{
    QJsonObject toolMenu;
    toolMenu[QStringLiteral("title")]  = QStringLiteral("QML Package");
    toolMenu[QStringLiteral("source")] = QStringLiteral("qml/View.qml"); // package-relative
    QJsonObject contributes;
    contributes[QStringLiteral("toolMenu")] = toolMenu;

    const QString packageDir = _writePackage(QStringLiteral("org.test.qmlpackage"), qmlPackageManifestJson(contributes));
    QVERIFY(!packageDir.isEmpty());

    PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Discovered);
    QCOMPARE(info.manifest.tier, PluginManifest::Tier::Qml);
    QCOMPARE(info.packageDir, packageDir);
    QCOMPARE(info.contributions.toolMenuItem[QStringLiteral("source")].toString(),
             QStringLiteral("file://%1/qml/View.qml").arg(packageDir));

    QGCPluginLoader::activate(info);
    QCOMPARE(info.state, PluginState::Active);
    QVERIFY(info.plugin == nullptr); // no binary at all — nothing to instantiate (D1)
}

void PluginLoaderGateTest::_inspectSdkPackageLoadsBinary_test()
{
    const QString packageDir = _writePackage(QStringLiteral("org.test.sdkpackage"), sdkPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());

    const QString binDir = createSubDir(QStringLiteral("org.test.sdkpackage/bin/macos-universal"));
    QVERIFY(!binDir.isEmpty());
    const QString dylibPath = binDir + QStringLiteral("/SdkPackage.dylib");
    QVERIFY(QFile::copy(testPluginFixturePath(), dylibPath));

    PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Discovered);
    QCOMPARE(info.manifest.tier, PluginManifest::Tier::Sdk);
    QCOMPARE(info.packageDir, packageDir);
    QCOMPARE(info.filePath, dylibPath); // resolved via the documented bin/macos-universal key

    QGCPluginLoader::activate(info);
    QCOMPARE(info.state, PluginState::Active);
    QVERIFY(info.plugin != nullptr);
    delete info.plugin;
}

void PluginLoaderGateTest::_inspectPackageBadLayoutErrorsLegible_test()
{
    // No bin/ directory at all
    const QString noBinDir = _writePackage(QStringLiteral("org.test.nobin"), sdkPackageManifestJson());
    QVERIFY(!noBinDir.isEmpty());
    const PluginLoadInfo noBinInfo = QGCPluginLoader::inspectPackage(noBinDir);
    QCOMPARE(noBinInfo.state, PluginState::Failed);
    QVERIFY2(noBinInfo.errorString.contains(QStringLiteral("bin/macos")), qPrintable(noBinInfo.errorString));

    // bin/ present but empty
    const QString emptyBinDir = _writePackage(QStringLiteral("org.test.emptybin"), sdkPackageManifestJson());
    QVERIFY(!emptyBinDir.isEmpty());
    QVERIFY(!createSubDir(QStringLiteral("org.test.emptybin/bin/macos-universal")).isEmpty());
    const PluginLoadInfo emptyBinInfo = QGCPluginLoader::inspectPackage(emptyBinDir);
    QCOMPARE(emptyBinInfo.state, PluginState::Failed);
    QVERIFY2(emptyBinInfo.errorString.contains(QStringLiteral("bin/macos")), qPrintable(emptyBinInfo.errorString));

    // Two binaries under the documented key subdirectory: ambiguous, not a guess
    const QString ambiguousDir = _writePackage(QStringLiteral("org.test.ambiguous"), sdkPackageManifestJson());
    QVERIFY(!ambiguousDir.isEmpty());
    const QString ambiguousBinDir = createSubDir(QStringLiteral("org.test.ambiguous/bin/macos-universal"));
    QVERIFY(!ambiguousBinDir.isEmpty());
    QVERIFY(QFile::copy(testPluginFixturePath(), ambiguousBinDir + QStringLiteral("/One.dylib")));
    QVERIFY(QFile::copy(testPluginFixturePath(), ambiguousBinDir + QStringLiteral("/Two.dylib")));
    const PluginLoadInfo ambiguousInfo = QGCPluginLoader::inspectPackage(ambiguousDir);
    QCOMPARE(ambiguousInfo.state, PluginState::Failed);
    QVERIFY2(ambiguousInfo.errorString.contains(QStringLiteral("multiple")), qPrintable(ambiguousInfo.errorString));
}

void PluginLoaderGateTest::_inspectQmlPackageRejectsBinary_test()
{
    const QString packageDir = _writePackage(QStringLiteral("org.test.qmlwithbinary"), qmlPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    const QString binDir = createSubDir(QStringLiteral("org.test.qmlwithbinary/bin/macos-universal"));
    QVERIFY(!binDir.isEmpty());
    QVERIFY(QFile::copy(testPluginFixturePath(), binDir + QStringLiteral("/Unexpected.dylib")));

    const PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Failed);
    QVERIFY2(info.errorString.contains(QStringLiteral("must not ship a binary")), qPrintable(info.errorString));
}

void PluginLoaderGateTest::_inspectQmlPackageRejectsReplayDeclaration_test()
{
    QJsonObject contributes;
    contributes[QStringLiteral("replay")] = true;

    const QString packageDir = _writePackage(QStringLiteral("org.test.qmlreplay"), qmlPackageManifestJson(contributes));
    QVERIFY(!packageDir.isEmpty());

    const PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Failed);
    QVERIFY2(info.errorString.contains(QStringLiteral("replay")), qPrintable(info.errorString));
}

void PluginLoaderGateTest::_inspectDirectoriesDiscoversPackagesAlongsideBareDylibs_test()
{
    const QString packageDir = _writePackage(QStringLiteral("org.test.discoveredpackage"), qmlPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    QVERIFY(QFile::copy(testPluginFixturePath(), tempPath(QStringLiteral("BareDylib.dylib"))));

    const QList<PluginLoadInfo> infos = QGCPluginLoader::inspectDirectories({tempDirPath()});

    bool foundPackage = false;
    bool foundBareDylib = false;
    for (const PluginLoadInfo& info : infos) {
        if (info.manifest.id == QStringLiteral("org.test.qmlpackage")) {
            foundPackage = true;
            QCOMPARE(info.state, PluginState::Discovered);
            QCOMPARE(info.packageDir, packageDir);
        } else if (info.manifest.id == QStringLiteral("org.qgroundcontrol.test.pluginloadergate")) {
            foundBareDylib = true;
            QVERIFY(info.packageDir.isEmpty());
        }
    }
    QVERIFY(foundPackage);
    QVERIFY(foundBareDylib);
}

void PluginLoaderGateTest::_inspectPackageRejectsInternalTier_test()
{
    // Tier internal is dev-loop only (hostBuildId-gated to a same-commit build, D7);
    // packaging it would decouple the sidecar manifest's trust from the code that
    // actually runs (nothing re-checks the bin/ binary against it). Packages are
    // sdk/qml only.
    const QString packageDir = _writePackage(QStringLiteral("org.test.internalpackage"), internalPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());

    const PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Failed);
    QVERIFY2(info.errorString.contains(QStringLiteral("internal")), qPrintable(info.errorString));
}

UT_REGISTER_TEST(PluginLoaderGateTest, TestLabel::Unit)
