#include "PluginTrustGateTest.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>

#include "PluginInstaller.h"
#include "PluginRecordStore.h"
#include "PluginTrustGate.h"
#include "QGCPluginInterface.h"

namespace {

// A minimal Discovered record with a real file on disk at filePath — consentDigest()
// needs a readable file, isUserDirPlugin() only needs the path to exist as a string.
PluginLoadInfo recordFixture(const QString& id, const QString& filePath)
{
    PluginLoadInfo info;
    info.filePath = filePath;
    info.state = PluginState::Discovered;
    info.manifest.id = id;
    info.manifest.name = id;
    info.manifest.version = QVersionNumber(1, 0, 0);
    info.manifest.vendor = QStringLiteral("Test Org");
    info.manifest.tier = PluginManifest::Tier::HostPinned;
    info.manifest.apiVersion = QGCPluginApiVersion;
    return info;
}

} // namespace

void PluginTrustGateTest::init()
{
    TempDirectoryTest::init();

    // Redirects QStandardPaths::AppDataLocation (and therefore
    // PluginInstaller::userPluginsDir()) to a Qt-managed, test-scoped location instead
    // of the developer's real application-support directory — mirrors
    // QGCPluginManagerTest::init(). Unconditional in init() (not left to each test's
    // trailing cleanup) so a QVERIFY/QCOMPARE failure mid-test can't leave a fixture
    // or a crash-quarantine marker in the real app config.
    QStandardPaths::setTestModeEnabled(true);
    QDir(PluginInstaller::userPluginsDir()).removeRecursively();

    QSettings settings;
    settings.remove(QStringLiteral("Plugins/ApprovedDigests"));
    settings.remove(QStringLiteral("PluginSystem/loadingPluginId"));
    settings.remove(QStringLiteral("PluginSystem/crashedPluginId"));
}

void PluginTrustGateTest::_isUserDirPluginTrueForUserDirContainer_test()
{
    const QString userPluginsDir = PluginInstaller::userPluginsDir();
    QVERIFY(!userPluginsDir.isEmpty());

    const QString filePath = QDir(userPluginsDir).filePath(QStringLiteral("org.test.userdir.dylib"));
    PluginLoadInfo record = recordFixture(QStringLiteral("org.test.userdir"), filePath);

    QVERIFY(PluginTrustGate::isUserDirPlugin(record));
}

void PluginTrustGateTest::_isUserDirPluginFalseForOtherContainer_test()
{
    const QString filePath = tempPath(QStringLiteral("org.test.other.dylib"));
    PluginLoadInfo record = recordFixture(QStringLiteral("org.test.other"), filePath);

    QVERIFY(!PluginTrustGate::isUserDirPlugin(record));
}

void PluginTrustGateTest::_isUserDirPluginFalseWhenUserPluginsDirEmpty_test()
{
    // An empty container path (no file/package at all) never matches the user dir.
    PluginLoadInfo record = recordFixture(QStringLiteral("org.test.empty"), QString());
    QVERIFY(!PluginTrustGate::isUserDirPlugin(record));
}

void PluginTrustGateTest::_consentDigestStableForUnchangedContent_test()
{
    const QString filePath = tempPath(QStringLiteral("stable.dylib"));
    QVERIFY(createFile(QStringLiteral("stable.dylib"), QByteArrayLiteral("fixture content")));
    PluginLoadInfo record = recordFixture(QStringLiteral("org.test.stable"), filePath);

    const QString first = PluginTrustGate::consentDigest(record);
    const QString second = PluginTrustGate::consentDigest(record);

    QVERIFY(!first.isEmpty());
    QCOMPARE(first, second);
}

void PluginTrustGateTest::_consentDigestChangesWithContent_test()
{
    const QString filePath = tempPath(QStringLiteral("changing.dylib"));
    QVERIFY(createFile(QStringLiteral("changing.dylib"), QByteArrayLiteral("original content")));
    PluginLoadInfo record = recordFixture(QStringLiteral("org.test.changing"), filePath);
    const QString before = PluginTrustGate::consentDigest(record);
    QVERIFY(!before.isEmpty());

    QVERIFY(createFile(QStringLiteral("changing.dylib"), QByteArrayLiteral("modified content")));
    const QString after = PluginTrustGate::consentDigest(record);

    QVERIFY(!after.isEmpty());
    QVERIFY(before != after);
}

void PluginTrustGateTest::_consentDigestEmptyOnUnreadableFile_test()
{
    PluginLoadInfo record = recordFixture(QStringLiteral("org.test.missing"), tempPath(QStringLiteral("does-not-exist.dylib")));

    QVERIFY(PluginTrustGate::consentDigest(record).isEmpty());
}

void PluginTrustGateTest::_applyTrustGateSkipsNonDiscoveredRecord_test()
{
    PluginLoadInfo record = recordFixture(QStringLiteral("org.test.notdiscovered"), tempPath(QStringLiteral("x.dylib")));
    record.state = PluginState::Failed;
    record.errorString = QStringLiteral("unchanged");

    PluginRecordStore store;
    PluginTrustGate::applyTrustGate(record, store);

    QCOMPARE(record.state, PluginState::Failed);
    QCOMPARE(record.errorString, QStringLiteral("unchanged"));
}

void PluginTrustGateTest::_applyTrustGateCrashSentinelOutranksConsent_test()
{
    const QString id = QStringLiteral("org.test.crashedgate");
    const QString userPluginsDir = PluginInstaller::userPluginsDir();
    QVERIFY(!userPluginsDir.isEmpty());
    const QString filePath = QDir(userPluginsDir).filePath(id + QStringLiteral(".dylib"));

    PluginLoadInfo record = recordFixture(id, filePath);

    QSettings settings;
    settings.setValue(QStringLiteral("PluginSystem/crashedPluginId"), id);
    PluginRecordStore store;
    store.checkCrashSentinel();
    QCOMPARE(store.crashedPluginId(), id);

    PluginTrustGate::applyTrustGate(record, store);

    // Crash quarantine wins even though this is an (unapproved) user-dir plugin —
    // consent is never consulted once the crash sentinel matches.
    QCOMPARE(record.state, PluginState::Quarantined);

    settings.remove(QStringLiteral("PluginSystem/crashedPluginId"));
    settings.sync();
}

void PluginTrustGateTest::_applyTrustGateBundleDirTrustedOnFirstSight_test()
{
    const QString filePath = tempPath(QStringLiteral("bundled.dylib"));
    QVERIFY(createFile(QStringLiteral("bundled.dylib"), QByteArrayLiteral("bundle fixture")));
    PluginLoadInfo record = recordFixture(QStringLiteral("org.test.bundled"), filePath);

    PluginRecordStore store;
    PluginTrustGate::applyTrustGate(record, store);

    // Not under the user plugins dir: trusted, no consent digest ever consulted.
    QCOMPARE(record.state, PluginState::Discovered);
    QVERIFY(record.errorString.isEmpty());
}

void PluginTrustGateTest::_applyTrustGateUserDirNeedsApprovalFirstSight_test()
{
    const QString id = QStringLiteral("org.test.needsapproval");
    const QString userPluginsDir = PluginInstaller::userPluginsDir();
    QVERIFY(!userPluginsDir.isEmpty());
    QVERIFY(QDir().mkpath(userPluginsDir));
    const QString filePath = QDir(userPluginsDir).filePath(id + QStringLiteral(".dylib"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(QByteArrayLiteral("user dir fixture")), qint64(16));
    file.close();

    PluginLoadInfo record = recordFixture(id, filePath);
    PluginRecordStore store;

    PluginTrustGate::applyTrustGate(record, store);

    QCOMPARE(record.state, PluginState::NeedsApproval);
    QVERIFY2(record.errorString.contains(QStringLiteral("New plugin")), qPrintable(record.errorString));

    QFile::remove(filePath);
}

void PluginTrustGateTest::_applyTrustGateUserDirApprovedDigestPasses_test()
{
    const QString id = QStringLiteral("org.test.approved");
    const QString userPluginsDir = PluginInstaller::userPluginsDir();
    QVERIFY(!userPluginsDir.isEmpty());
    QVERIFY(QDir().mkpath(userPluginsDir));
    const QString filePath = QDir(userPluginsDir).filePath(id + QStringLiteral(".dylib"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(QByteArrayLiteral("approved fixture")), qint64(16));
    file.close();

    PluginLoadInfo record = recordFixture(id, filePath);
    PluginRecordStore store;
    const QString digest = PluginTrustGate::consentDigest(record);
    QVERIFY(!digest.isEmpty());
    store.setApprovedPluginDigest(id, digest);

    PluginTrustGate::applyTrustGate(record, store);

    // Matching recorded digest: stays Discovered, ready for activation.
    QCOMPARE(record.state, PluginState::Discovered);
    QVERIFY(record.errorString.isEmpty());

    store.setApprovedPluginDigest(id, QString());
    QFile::remove(filePath);
}

void PluginTrustGateTest::_applyTrustGateUserDirChangedContentReprompts_test()
{
    const QString id = QStringLiteral("org.test.reprompt");
    const QString userPluginsDir = PluginInstaller::userPluginsDir();
    QVERIFY(!userPluginsDir.isEmpty());
    QVERIFY(QDir().mkpath(userPluginsDir));
    const QString filePath = QDir(userPluginsDir).filePath(id + QStringLiteral(".dylib"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(QByteArrayLiteral("original content")), qint64(16));
    file.close();

    PluginLoadInfo record = recordFixture(id, filePath);
    PluginRecordStore store;
    store.setApprovedPluginDigest(id, PluginTrustGate::consentDigest(record));

    // Content changes after approval: the recorded digest no longer matches.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(QByteArrayLiteral("modified content")), qint64(16));
    file.close();

    PluginTrustGate::applyTrustGate(record, store);

    QCOMPARE(record.state, PluginState::NeedsApproval);
    QVERIFY2(record.errorString.contains(QStringLiteral("changed")), qPrintable(record.errorString));

    store.setApprovedPluginDigest(id, QString());
    QFile::remove(filePath);
}

UT_REGISTER_TEST(PluginTrustGateTest, TestLabel::Unit)
