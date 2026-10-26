#include "PluginLoaderGateTest.h"

#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPluginLoader>
#include <QtCore/QUrl>

#include "PluginManifest.h"
#include "PluginRecordStore.h"
#include "PluginTrustGate.h"
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

// Path to a second, independently-built dylib from the identical sources/manifest
// (same plugin id and version) — TestPlugin/CMakeLists.txt's TestPluginFixtureV2
// target, injected the same way as the primary fixture.
QString testPluginFixtureV2Path()
{
    return QStringLiteral(QGC_TEST_PLUGIN_FIXTURE_V2_PATH);
}

// The bin/ subdirectory inspectPackage() resolves a binary from on this platform.
QString platformBinSubdir(const QString& packageName)
{
    return packageName + QStringLiteral("/bin/") + QGCPluginLoader::platformKey();
}

// <dir>/<baseName> with this platform's plugin suffix, taken from the real fixture.
QString pluginFilePath(const QString& dir, const QString& baseName)
{
    return dir + QLatin1Char('/') + baseName + QLatin1Char('.') + QFileInfo(testPluginFixturePath()).suffix();
}

// Copies the real fixture to dstPath with every Qt metadata header claiming Qt minor `minor`. The
// header is 4 bytes: format version, Qt major, Qt minor, arch byte. ELF holds it in a note named
// "qt-project!" (type 0x74510001), right after the name; other formats put it after the
// "QTMETADATA !" magic, once per slice of a universal Mach-O. Returns the number of headers patched.
int copyFixtureWithQtMinor(const QString& dstPath, int minor)
{
    QFile src(testPluginFixturePath());
    if (!src.open(QIODevice::ReadOnly)) {
        return 0;
    }
    QByteArray bytes = src.readAll();

#if defined(Q_OF_ELF)
    const QByteArray marker = QByteArrayLiteral("\x01\x00\x51\x74qt-project!\x00");
#else
    const QByteArray marker = QByteArrayLiteral("QTMETADATA !");
#endif
    const qsizetype minorOffset = marker.size() + 2;

    int patched = 0;
    for (qsizetype at = bytes.indexOf(marker); at >= 0 && at + minorOffset < bytes.size();
         at = bytes.indexOf(marker, at + 1)) {
        bytes[at + minorOffset] = static_cast<char>(minor);
        ++patched;
    }

    QFile dst(dstPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate) || dst.write(bytes) != bytes.size()) {
        return 0;
    }
    return patched;
}

// The Qt minor a binary's header claims, as QPluginLoader reports it without loading the binary.
int metaDataQtMinor(const QString& path)
{
    return (QPluginLoader(path).metaData().value(QStringLiteral("version")).toInt() >> 8) & 0xff;
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

    // The build Qt and Debug flag come from the binary's own metadata header
    QCOMPARE(info.manifest.qtVersion, QVersionNumber(QT_VERSION_MAJOR, QT_VERSION_MINOR));
    QVERIFY(info.manifest.debugBuild.has_value());
    QCOMPARE(*info.manifest.debugBuild, QGCPluginLoader::hostInfo().debugBuild);

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

// U1's proof: buildMarker is read from the mapped image, not the file. Two
// separate builds of identical sources/manifest (same plugin id and version)
// must report two different markers, and re-activating the same file twice
// must report the same marker both times (deterministic per build, not
// per-call) — otherwise it would be indistinguishable from noise.
void PluginLoaderGateTest::_buildMarkerDistinguishesSeparateBuilds_test()
{
    PluginLoadInfo first = QGCPluginLoader::inspect(testPluginFixturePath());
    QCOMPARE(first.state, PluginState::Discovered);
    QGCPluginLoader::activate(first);
    QCOMPARE(first.state, PluginState::Active);
    QVERIFY(!first.buildMarker.isEmpty());
    delete first.plugin;

    PluginLoadInfo firstAgain = QGCPluginLoader::inspect(testPluginFixturePath());
    QGCPluginLoader::activate(firstAgain);
    QCOMPARE(firstAgain.state, PluginState::Active);
    QCOMPARE(firstAgain.buildMarker, first.buildMarker);
    delete firstAgain.plugin;

    PluginLoadInfo second = QGCPluginLoader::inspect(testPluginFixtureV2Path());
    QCOMPARE(second.state, PluginState::Discovered);
    QCOMPARE(second.manifest.id, first.manifest.id);
    QGCPluginLoader::activate(second);
    QCOMPARE(second.state, PluginState::Active);
    QVERIFY(!second.buildMarker.isEmpty());
    QVERIFY(second.buildMarker != first.buildMarker);
    delete second.plugin;
}

// U1's risk item, measured: does an in-place upgrade (deactivate, overwrite the
// binary at the same package path, re-inspect+reactivate — installPlugin's shape)
// actually run the new image, or does the old one stay mapped and keep executing?
// Answer recorded in the plan doc regardless of outcome (04 "Risks & spikes").
void PluginLoaderGateTest::_inPlaceUpgradeChangesBuildMarker_test()
{
    const QString packageDir = _writePackage(QStringLiteral("org.test.upgrade"), sdkPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    const QString binDir = createSubDir(platformBinSubdir(QStringLiteral("org.test.upgrade")));
    QVERIFY(!binDir.isEmpty());
    const QString dylibPath = pluginFilePath(binDir, QStringLiteral("UpgradePackage"));

    QVERIFY(QFile::copy(testPluginFixturePath(), dylibPath));
    PluginLoadInfo before = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(before.state, PluginState::Discovered);
    QGCPluginLoader::activate(before);
    QCOMPARE(before.state, PluginState::Active);
    QVERIFY(!before.buildMarker.isEmpty());
    delete before.plugin;

    // Overwrite the same path with a different build — installPlugin's shape: the old
    // image is deactivated (deleted above) but stays mapped (PreventUnloadHint, S2/S3);
    // only the file on disk changes before the fresh inspect+activate.
    QVERIFY(QFile::remove(dylibPath));
    QVERIFY(QFile::copy(testPluginFixtureV2Path(), dylibPath));
    PluginLoadInfo after = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(after.state, PluginState::Discovered);
    QGCPluginLoader::activate(after);
    QCOMPARE(after.state, PluginState::Active);
    QVERIFY(!after.buildMarker.isEmpty());
    delete after.plugin;

    // The measured answer (see the plan doc's Risks & spikes section): QPluginLoader
    // keys its instance cache by file path and reuses the already-mapped image, so a
    // same-path in-place upgrade keeps executing the OLD build's code even though the
    // manifest and file on disk are the new one's.
    QCOMPARE(after.buildMarker, before.buildMarker);
}

void PluginLoaderGateTest::_inspectMissingFileFails_test()
{
    const PluginLoadInfo info = QGCPluginLoader::inspect(QStringLiteral("/nonexistent/NoSuchPlugin.dylib"));

    QCOMPARE(info.state, PluginState::Failed);
    QVERIFY(!info.errorString.isEmpty());
}

// The permanent "different-SDK-zip-host" ABI proof (04 §11 Spike S3, DoD #1's residue):
// a dylib built against a standalone SDK zip must still satisfy today's QGCPluginLoader
// gate. CI points this at a real path via env var; outside CI (or before U5.1's
// macos.yml wiring lands) the var is unset and the slot skips rather than failing.
//
// Exercises both compiled-in vtables a plugin binary carries: QGCPluginInterface's
// (pluginInterfaceVersion()/createPlugin(), invoked by activate() itself) and
// QGCPlugin's own (replayExtension(), invoked explicitly below, mirroring
// _activateRealPlugin_test's real-fixture check).
void PluginLoaderGateTest::_inspectAndActivateExternalDylib(const QByteArray& envVarName, const QString& skipContext)
{
    const QString path = qEnvironmentVariable(envVarName.constData());
    if (path.isEmpty()) {
        QSKIP(qPrintable(QStringLiteral("%1 not set; skipping %2 ABI check (set by CI)")
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
             QUrl::fromLocalFile(packageDir + QStringLiteral("/qml/View.qml")).toString());
    QVERIFY(QUrl(info.contributions.toolMenuItem[QStringLiteral("source")].toString()).isLocalFile());

    QGCPluginLoader::activate(info);
    QCOMPARE(info.state, PluginState::Active);
    QVERIFY(info.plugin == nullptr); // no binary at all — nothing to instantiate (D1)
}

void PluginLoaderGateTest::_inspectSdkPackageLoadsBinary_test()
{
    const QString packageDir = _writePackage(QStringLiteral("org.test.sdkpackage"), sdkPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());

    const QString binDir = createSubDir(platformBinSubdir(QStringLiteral("org.test.sdkpackage")));
    QVERIFY(!binDir.isEmpty());
    const QString dylibPath = pluginFilePath(binDir, QStringLiteral("SdkPackage"));
    QVERIFY(QFile::copy(testPluginFixturePath(), dylibPath));

    PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Discovered);
    QCOMPARE(info.manifest.tier, PluginManifest::Tier::Sdk);
    QCOMPARE(info.packageDir, packageDir);
    // Resolved via the documented bin/<platformKey> subdirectory
    QCOMPARE(info.filePath, dylibPath);

    QGCPluginLoader::activate(info);
    QCOMPARE(info.state, PluginState::Active);
    QVERIFY(info.plugin != nullptr);
    delete info.plugin;
}

void PluginLoaderGateTest::_inspectPackageBadLayoutErrorsLegible_test()
{
    const QString binKey = QStringLiteral("bin/") + QGCPluginLoader::platformKey();

    // No bin/ directory at all
    const QString noBinDir = _writePackage(QStringLiteral("org.test.nobin"), sdkPackageManifestJson());
    QVERIFY(!noBinDir.isEmpty());
    const PluginLoadInfo noBinInfo = QGCPluginLoader::inspectPackage(noBinDir);
    QCOMPARE(noBinInfo.state, PluginState::Failed);
    QVERIFY2(noBinInfo.errorString.contains(binKey), qPrintable(noBinInfo.errorString));

    // bin/ present but empty
    const QString emptyBinDir = _writePackage(QStringLiteral("org.test.emptybin"), sdkPackageManifestJson());
    QVERIFY(!emptyBinDir.isEmpty());
    QVERIFY(!createSubDir(platformBinSubdir(QStringLiteral("org.test.emptybin"))).isEmpty());
    const PluginLoadInfo emptyBinInfo = QGCPluginLoader::inspectPackage(emptyBinDir);
    QCOMPARE(emptyBinInfo.state, PluginState::Failed);
    QVERIFY2(emptyBinInfo.errorString.contains(binKey), qPrintable(emptyBinInfo.errorString));

    // Two binaries under the documented key subdirectory: ambiguous, not a guess
    const QString ambiguousDir = _writePackage(QStringLiteral("org.test.ambiguous"), sdkPackageManifestJson());
    QVERIFY(!ambiguousDir.isEmpty());
    const QString ambiguousBinDir = createSubDir(platformBinSubdir(QStringLiteral("org.test.ambiguous")));
    QVERIFY(!ambiguousBinDir.isEmpty());
    QVERIFY(QFile::copy(testPluginFixturePath(), pluginFilePath(ambiguousBinDir, QStringLiteral("One"))));
    QVERIFY(QFile::copy(testPluginFixturePath(), pluginFilePath(ambiguousBinDir, QStringLiteral("Two"))));
    const PluginLoadInfo ambiguousInfo = QGCPluginLoader::inspectPackage(ambiguousDir);
    QCOMPARE(ambiguousInfo.state, PluginState::Failed);
    QVERIFY2(ambiguousInfo.errorString.contains(QStringLiteral("multiple")), qPrintable(ambiguousInfo.errorString));
}

void PluginLoaderGateTest::_platformKeyMatchesBuild_test()
{
#if defined(Q_OS_MACOS)
    QCOMPARE(QGCPluginLoader::platformKey(), QStringLiteral("macos-universal"));
#elif defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    QCOMPARE(QGCPluginLoader::platformKey(), QStringLiteral("windows-x64"));
#elif defined(Q_OS_ANDROID) && defined(Q_PROCESSOR_ARM_64)
    QCOMPARE(QGCPluginLoader::platformKey(), QStringLiteral("android-arm64"));
#elif defined(Q_OS_ANDROID) && defined(Q_PROCESSOR_X86_64)
    QCOMPARE(QGCPluginLoader::platformKey(), QStringLiteral("android-x64"));
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    QCOMPARE(QGCPluginLoader::platformKey(), QStringLiteral("linux-x64"));
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_ARM_64)
    QCOMPARE(QGCPluginLoader::platformKey(), QStringLiteral("linux-arm64"));
#else
    QVERIFY(QGCPluginLoader::platformKey().endsWith(QStringLiteral("-unknown")));
#endif
}

void PluginLoaderGateTest::_inspectPackageRejectsForeignArchBinary_test()
{
#if defined(Q_OS_MACOS)
    QSKIP("macOS accepts any bin/macos-*/ binary; the exact-key rule applies to the other OSes");
#else
    const QString key = QGCPluginLoader::platformKey();
    const QString osName = key.section(QLatin1Char('-'), 0, 0);
    const bool isX64 = key.endsWith(QStringLiteral("-x64"));
    const QString foreignKey = osName + (isX64 ? QStringLiteral("-arm64") : QStringLiteral("-x64"));

    const QString packageDir = _writePackage(QStringLiteral("org.test.foreignarch"), sdkPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    const QString binDir = createSubDir(QStringLiteral("org.test.foreignarch/bin/") + foreignKey);
    QVERIFY(!binDir.isEmpty());
    QVERIFY(QFile::copy(testPluginFixturePath(), pluginFilePath(binDir, QStringLiteral("Foreign"))));

    const PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Failed);
    QVERIFY2(info.errorString.contains(QStringLiteral("bin/") + key), qPrintable(info.errorString));
    QVERIFY2(!info.errorString.contains(QStringLiteral("-*")), qPrintable(info.errorString));
#endif
}

void PluginLoaderGateTest::_inspectPackageRejectsNonPluginBinary_test()
{
    const QString packageDir = _writePackage(QStringLiteral("org.test.notaplugin"), sdkPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    const QString binDir = createSubDir(platformBinSubdir(QStringLiteral("org.test.notaplugin")));
    QVERIFY(!binDir.isEmpty());
    QFile binary(pluginFilePath(binDir, QStringLiteral("NotAPlugin")));
    QVERIFY(binary.open(QIODevice::WriteOnly));
    binary.write("this is not a Qt plugin");
    binary.close();

    const PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Failed);
    QVERIFY2(!info.errorString.isEmpty(), "a rejected binary must say why");
}

void PluginLoaderGateTest::_inspectPackageMacFallbackFindsSingleArchBinary_test()
{
#if !defined(Q_OS_MACOS)
    QSKIP("the bin/macos-*/ fallback is macOS only");
#else
    const QString packageDir = _writePackage(QStringLiteral("org.test.macfallback"), sdkPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    const QString binDir = createSubDir(QStringLiteral("org.test.macfallback/bin/macos-x86_64"));
    QVERIFY(!binDir.isEmpty());
    const QString dylibPath = pluginFilePath(binDir, QStringLiteral("SingleArch"));
    QVERIFY(QFile::copy(testPluginFixturePath(), dylibPath));

    const PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Discovered);
    QCOMPARE(info.filePath, dylibPath);
#endif
}

void PluginLoaderGateTest::_inspectQmlPackageRejectsBinary_test()
{
    const QString packageDir = _writePackage(QStringLiteral("org.test.qmlwithbinary"), qmlPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    const QString binDir = createSubDir(platformBinSubdir(QStringLiteral("org.test.qmlwithbinary")));
    QVERIFY(!binDir.isEmpty());
    QVERIFY(QFile::copy(testPluginFixturePath(), pluginFilePath(binDir, QStringLiteral("Unexpected"))));

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
    QVERIFY(QFile::copy(testPluginFixturePath(), pluginFilePath(tempDirPath(), QStringLiteral("BareDylib"))));

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

// The catalog plan's S4 case: a binary built with a newer Qt minor passed inspection and failed only
// at activation, after the user had approved it. It must be refused at inspection instead.
void PluginLoaderGateTest::_inspectRefusesNewerQtMinor_test()
{
    const QVersionNumber hostQt = QGCPluginLoader::hostInfo().qtVersion;
    QVERIFY(!hostQt.isNull());
    const int newerMinor = hostQt.minorVersion() + 1;
    const QString expectedReason = QStringLiteral("built with Qt %1.%2; this QGroundControl runs Qt %1.%3")
                                       .arg(hostQt.majorVersion())
                                       .arg(newerMinor)
                                       .arg(hostQt.minorVersion());

    // Bare dev-loop file
    const QString barePath = pluginFilePath(tempDirPath(), QStringLiteral("NewerQtBare"));
    QVERIFY(copyFixtureWithQtMinor(barePath, newerMinor) > 0);
    QCOMPARE(metaDataQtMinor(barePath), newerMinor);  // the patch reached the header Qt reads

    const PluginLoadInfo bare = QGCPluginLoader::inspect(barePath);
    QCOMPARE(bare.state, PluginState::Incompatible);
    QCOMPARE(bare.errorString, expectedReason);

    // Package: the sidecar qgcplugin.json is valid; only the binary's header is newer
    const QString packageDir = _writePackage(QStringLiteral("org.test.newerqt"), sdkPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    const QString binDir = createSubDir(platformBinSubdir(QStringLiteral("org.test.newerqt")));
    QVERIFY(!binDir.isEmpty());
    QVERIFY(copyFixtureWithQtMinor(pluginFilePath(binDir, QStringLiteral("NewerQt")), newerMinor) > 0);

    PluginLoadInfo package = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(package.state, PluginState::Incompatible);
    QCOMPARE(package.errorString, expectedReason);

    // The consent gate acts only on a Discovered record, so a refused one never awaits approval
    PluginRecordStore store;
    PluginTrustGate::applyTrustGate(package, store);
    QCOMPARE(package.state, PluginState::Incompatible);
}

// Control for the patch itself: the same rewrite to an older minor still inspects clean.
void PluginLoaderGateTest::_inspectAcceptsOlderQtMinor_test()
{
    const QVersionNumber hostQt = QGCPluginLoader::hostInfo().qtVersion;
    if (hostQt.minorVersion() == 0) {
        QSKIP("host runs Qt x.0; there is no older minor in this major");
    }
    const int olderMinor = hostQt.minorVersion() - 1;

    const QString packageDir = _writePackage(QStringLiteral("org.test.olderqt"), sdkPackageManifestJson());
    QVERIFY(!packageDir.isEmpty());
    const QString binDir = createSubDir(platformBinSubdir(QStringLiteral("org.test.olderqt")));
    QVERIFY(!binDir.isEmpty());
    QVERIFY(copyFixtureWithQtMinor(pluginFilePath(binDir, QStringLiteral("OlderQt")), olderMinor) > 0);

    const PluginLoadInfo info = QGCPluginLoader::inspectPackage(packageDir);
    QCOMPARE(info.state, PluginState::Discovered);
    QCOMPARE(info.manifest.qtVersion, QVersionNumber(hostQt.majorVersion(), olderMinor));
}

UT_REGISTER_TEST(PluginLoaderGateTest, TestLabel::Unit)
