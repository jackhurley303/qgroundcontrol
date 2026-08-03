#include "PluginRecordStoreTest.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>

#include "Fact.h"
#include "PluginRecordStore.h"
#include "PluginSettings.h"
#include "QGCPluginInterface.h"
#include "SettingsManager.h"

namespace {

// Crash-sentinel keys, mirroring PluginRecordStore's constants
constexpr const char* kLoadingPluginIdKey = "PluginSystem/loadingPluginId";
constexpr const char* kCrashedPluginIdKey = "PluginSystem/crashedPluginId";

PluginSettings* pluginSettings()
{
    return SettingsManager::instance()->pluginSettings();
}

// A minimal Discovered record — the fields find()/removeIf() key on.
PluginLoadInfo recordFixture(const QString& id)
{
    PluginLoadInfo info;
    info.filePath = QStringLiteral("/nonexistent/%1.dylib").arg(id);
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

void PluginRecordStoreTest::init()
{
    TempDirectoryTest::init();

    QSettings settings;
    settings.remove(QStringLiteral("Plugins/ApprovedDigests"));
    settings.remove(QString::fromLatin1(kLoadingPluginIdKey));
    settings.remove(QString::fromLatin1(kCrashedPluginIdKey));
}

void PluginRecordStoreTest::_findReturnsMatchingRecordById_test()
{
    PluginRecordStore store;
    store.append(recordFixture(QStringLiteral("org.test.alpha")));
    store.append(recordFixture(QStringLiteral("org.test.beta")));

    PluginLoadInfo* found = store.find(QStringLiteral("org.test.beta"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->manifest.id, QStringLiteral("org.test.beta"));
}

void PluginRecordStoreTest::_findReturnsFirstOfDuplicateIds_test()
{
    PluginRecordStore store;
    PluginLoadInfo first = recordFixture(QStringLiteral("org.test.dup"));
    first.errorString = QStringLiteral("first");
    PluginLoadInfo second = recordFixture(QStringLiteral("org.test.dup"));
    second.errorString = QStringLiteral("second");
    store.append(first);
    store.append(second);

    PluginLoadInfo* found = store.find(QStringLiteral("org.test.dup"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->errorString, QStringLiteral("first"));
}

void PluginRecordStoreTest::_findReturnsNullptrWhenAbsent_test()
{
    PluginRecordStore store;
    QVERIFY(store.find(QStringLiteral("org.test.absent")) == nullptr);
}

void PluginRecordStoreTest::_removeIfDropsMatchingRecords_test()
{
    PluginRecordStore store;
    store.append(recordFixture(QStringLiteral("org.test.keep")));
    store.append(recordFixture(QStringLiteral("org.test.drop")));

    store.removeIf([](const PluginLoadInfo& r) { return r.manifest.id == QStringLiteral("org.test.drop"); });

    QCOMPARE(store.records().size(), 1);
    QVERIFY(store.find(QStringLiteral("org.test.drop")) == nullptr);
    QVERIFY(store.find(QStringLiteral("org.test.keep")) != nullptr);
}

void PluginRecordStoreTest::_clearDropsRecordsButNotPersistedSentinel_test()
{
    PluginRecordStore store;
    store.append(recordFixture(QStringLiteral("org.test.cleared")));

    QSettings settings;
    settings.setValue(QString::fromLatin1(kCrashedPluginIdKey), QStringLiteral("org.test.crashed"));
    store.checkCrashSentinel();
    QCOMPARE(store.crashedPluginId(), QStringLiteral("org.test.crashed"));

    store.clear();

    QVERIFY(store.records().isEmpty());
    // clear() is a runtime teardown, not a factory reset: persisted crash state
    // (and the in-memory copy loaded from it) is untouched.
    QCOMPARE(store.crashedPluginId(), QStringLiteral("org.test.crashed"));
}

void PluginRecordStoreTest::_checkCrashSentinelPromotesLingeringId_test()
{
    const QString id = QStringLiteral("org.test.lingering");

    QSettings settings;
    settings.setValue(QString::fromLatin1(kLoadingPluginIdKey), id);

    PluginRecordStore store;
    expectLogMessage("PluginSystem.PluginRecordStore", QtWarningMsg, QRegularExpression("Previous run crashed while loading plugin"));
    store.checkCrashSentinel();
    verifyExpectedLogMessage();

    QCOMPARE(store.crashedPluginId(), id);
    settings.sync();
    QVERIFY(settings.value(QString::fromLatin1(kLoadingPluginIdKey)).toString().isEmpty());
    QCOMPARE(settings.value(QString::fromLatin1(kCrashedPluginIdKey)).toString(), id);
}

void PluginRecordStoreTest::_checkCrashSentinelNoOpWithoutLingeringId_test()
{
    PluginRecordStore store;
    store.checkCrashSentinel();

    QVERIFY(store.crashedPluginId().isEmpty());
    QSettings settings;
    settings.sync();
    QVERIFY(settings.value(QString::fromLatin1(kCrashedPluginIdKey)).toString().isEmpty());
}

void PluginRecordStoreTest::_clearCrashQuarantineClearsPersistedAndInMemory_test()
{
    const QString id = QStringLiteral("org.test.quarantined");

    QSettings settings;
    settings.setValue(QString::fromLatin1(kCrashedPluginIdKey), id);

    PluginRecordStore store;
    store.checkCrashSentinel();
    QCOMPARE(store.crashedPluginId(), id);

    store.clearCrashQuarantine();

    QVERIFY(store.crashedPluginId().isEmpty());
    settings.sync();
    QVERIFY(settings.value(QString::fromLatin1(kCrashedPluginIdKey)).toString().isEmpty());
}

void PluginRecordStoreTest::_armLoadingSentinelWritesThenClearsOnScopeExit_test()
{
    const QString id = QStringLiteral("org.test.arming");

    PluginRecordStore store;
    QSettings settings;

    {
        const auto guard = store.armLoadingSentinel(id);
        settings.sync();
        QCOMPARE(settings.value(QString::fromLatin1(kLoadingPluginIdKey)).toString(), id);
    }

    settings.sync();
    QVERIFY(settings.value(QString::fromLatin1(kLoadingPluginIdKey)).toString().isEmpty());

    // NOTE: this proves ordering (write happens before the guard's scope ends, clear
    // happens after) but NOT durability — every QSettings instance in this process
    // shares one in-memory cache, so a reader here can't distinguish a synced write
    // from an unsynced one. Deleting armLoadingSentinel()'s settings.sync() calls
    // (PluginRecordStore.cc) would still pass this test, even though sync() is the
    // actual crash-safety mechanism: without it, a mid-activation crash leaves nothing
    // on disk to blame at the next boot. A true durability test needs an out-of-band
    // read of the backing store (e.g. an explicit QSettings::IniFormat file, read via a
    // second QSettings instance pointed at the same path) — left as a follow-up rather
    // than adding a test-only settings backend to armLoadingSentinel() for this unit.
}

void PluginRecordStoreTest::_consentDigestRoundTrip_test()
{
    const QString id = QStringLiteral("org.test.consent");

    PluginRecordStore store;
    QVERIFY(store.approvedPluginDigest(id).isEmpty());

    store.setApprovedPluginDigest(id, QStringLiteral("1.0.0:abc123"));
    QCOMPARE(store.approvedPluginDigest(id), QStringLiteral("1.0.0:abc123"));

    store.setApprovedPluginDigest(id, QString());
    QVERIFY(store.approvedPluginDigest(id).isEmpty());
}

void PluginRecordStoreTest::_registerAndIsPluginEnabled_test()
{
    const QString id = QStringLiteral("org.test.registered");

    PluginRecordStore store;
    store.registerPlugin(id, QStringLiteral("Registered Plugin"), true);

    QVERIFY(store.isPluginEnabled(id));
    Fact* fact = store.pluginEnabledFact(id);
    QVERIFY(fact != nullptr);
    QCOMPARE(fact->label(), QStringLiteral("Registered Plugin"));

    fact->setRawValue(false);
    QVERIFY(!store.isPluginEnabled(id));

    // registerPlugin() and pluginEnabledFact() both go through the same
    // SettingsManager()->pluginSettings() the store owns exclusively.
    QCOMPARE(store.pluginEnabledFact(id), pluginSettings()->pluginEnabledFact(id));
}

UT_REGISTER_TEST(PluginRecordStoreTest, TestLabel::Unit)
