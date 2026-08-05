#include "QGCPluginManagerTest.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include "Fact.h"
#include "MultiSignalSpy.h"
#include "PluginContributions.h"
#include "PluginInstaller.h"
#include "PluginSettings.h"
#include "QGCPlugin.h"
#include "QGCPluginInterface.h"
#include "QGCPluginManager.h"
#include "QGCReplayExtension.h"
#include "SettingsManager.h"

#if defined(Q_OS_MACOS)
#include <sys/xattr.h>
#endif

namespace {

// A Discovered record whose file path does not exist: any (wrongful) activation
// attempt fails and flips the state to Failed, making it detectable.
PluginLoadInfo discoveredFixture(const QString& id, const QString& name)
{
    PluginLoadInfo info;
    info.filePath = QStringLiteral("/nonexistent/%1.dylib").arg(id);
    info.state = PluginState::Discovered;
    info.manifest.id = id;
    info.manifest.name = name;
    info.manifest.version = QVersionNumber(1, 0, 0);
    info.manifest.vendor = QStringLiteral("Test Org");
    info.manifest.tier = PluginManifest::Tier::HostPinned;
    info.manifest.apiVersion = QGCPluginApiVersion;
    return info;
}

PluginSettings* pluginSettings()
{
    return SettingsManager::instance()->pluginSettings();
}

// Crash-sentinel keys, mirroring QGCPluginManager's constants
constexpr const char* kLoadingPluginIdKey = "PluginSystem/loadingPluginId";
constexpr const char* kCrashedPluginIdKey = "PluginSystem/crashedPluginId";

// Minimal stub satisfying QGCReplayExtension's pure virtuals — never invoked,
// only its identity (pointer value) matters to the emission-ordering test.
class StubReplayExtension : public QGCReplayExtension
{
public:
    bool isActive() const override { return false; }
    QObject* logReplayLink() const override { return nullptr; }
    bool isPlaying() const override { return false; }
    qreal playbackSpeed() const override { return 1.0; }
    bool hasVideo() const override { return false; }
    QString videoUrl() const override { return QString(); }
    qreal videoOffsetSecs() const override { return 0.0; }
    qint64 videoPositionMs() const override { return 0; }
    qint64 videoDurationMs() const override { return 0; }
    void openFlight(QObject*) override {}
    void closeFlight() override {}
    void setPlaybackSpeed(qreal) override {}
    void seekTo(qreal) override {}
    void adjustVideoOffset(qreal) override {}
};

// A plugin whose replayExtension() always returns the same owned instance —
// for tests exercising _recalcReplayExtension()'s selection across records
// with a real (non-null) record.plugin.
class ReplayProvidingPlugin : public QGCPlugin
{
public:
    explicit ReplayProvidingPlugin(QObject* parent = nullptr)
        : QGCPlugin(parent), _extension(new StubReplayExtension)
    {
        _extension->setParent(this);
    }
    QGCReplayExtension* replayExtension() const override { return _extension; }

private:
    StubReplayExtension* _extension;
};

// An Active record with a real plugin instance declaring a replay extension —
// unlike discoveredFixture(), this is what _recalcReplayExtension() actually
// scans for (record.plugin non-null, contributions.providesReplayExtension).
PluginLoadInfo activeReplayProviderFixture(const QString& id, QObject* parent)
{
    PluginLoadInfo info;
    info.filePath = QStringLiteral("/nonexistent/%1.dylib").arg(id);
    info.state = PluginState::Active;
    info.manifest.id = id;
    info.manifest.name = id;
    info.manifest.version = QVersionNumber(1, 0, 0);
    info.manifest.vendor = QStringLiteral("Test Org");
    info.manifest.tier = PluginManifest::Tier::HostPinned;
    info.manifest.apiVersion = QGCPluginApiVersion;
    info.contributions.providesReplayExtension = true;
    info.plugin = new ReplayProvidingPlugin(parent);
    return info;
}

// Path to the real TestPluginFixture dylib, injected by
// test/PluginSystem/CMakeLists.txt as a compile definition. Its qrc registers
// TestPluginFixturePanel.qml into /qml when the library is loaded, which is the
// late registration _lateActivationResolvesPluginQml_test is about.
QString testPluginFixturePath()
{
    return QStringLiteral(QGC_TEST_PLUGIN_FIXTURE_PATH);
}

} // namespace

void QGCPluginManagerTest::init()
{
    TempDirectoryTest::init();

    // Redirects QStandardPaths::AppDataLocation (and therefore
    // PluginInstaller::userPluginsDir()) to a Qt-managed, test-scoped location instead
    // of the developer's real application-support directory.
    QStandardPaths::setTestModeEnabled(true);
    QDir(PluginInstaller::userPluginsDir()).removeRecursively();

    // Consent digests persist in QSettings across runs of this binary; the fixture
    // packages are byte-identical each run, so a stale approval would defeat the
    // first-sight assertions.
    QSettings settings;
    settings.remove(QStringLiteral("Plugins/ApprovedDigests"));

    // Crash-sentinel keys also persist across runs; a lingering value from an earlier
    // test (or a real crash of this binary) would quarantine unrelated fixtures.
    settings.remove(QString::fromLatin1(kLoadingPluginIdKey));
    settings.remove(QString::fromLatin1(kCrashedPluginIdKey));
}

QString QGCPluginManagerTest::_writePackage(const QString& parentDir, const QString& id, const QString& tier, const QString& description)
{
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("name")] = QStringLiteral("Test Package");
    json[QStringLiteral("version")] = QStringLiteral("1.0.0");
    json[QStringLiteral("vendor")] = QStringLiteral("Test Org");
    json[QStringLiteral("description")] = description;
    json[QStringLiteral("tier")] = tier;
    if (tier != QStringLiteral("qml")) {
        json[QStringLiteral("apiVersion")] = QGCPluginApiVersion;
    }
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    hostVersion[QStringLiteral("max")] = QString();
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("contributes")] = QJsonObject();

    const QDir packageDir(QDir(parentDir).filePath(id));
    if (!QDir().mkpath(packageDir.absolutePath())) {
        return QString();
    }
    QFile manifestFile(packageDir.filePath(QStringLiteral("qgcplugin.json")));
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || manifestFile.write(QJsonDocument(json).toJson()) < 0) {
        return QString();
    }
    manifestFile.close();

    if (tier != QStringLiteral("qml")) {
        // inspectPackage() requires exactly one binary under bin/; its content is never
        // read at inspection, so a placeholder is enough for gate tests.
        const QString binDir = packageDir.filePath(QStringLiteral("bin/macos-universal"));
        if (!QDir().mkpath(binDir)) {
            return QString();
        }
        QFile binary(binDir + QStringLiteral("/libfixture.dylib"));
        if (!binary.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || binary.write(QByteArrayLiteral("not a real dylib")) < 0) {
            return QString();
        }
    }

    return packageDir.absolutePath();
}

void QGCPluginManagerTest::_disabledNeverActivated_test()
{
    const QString id = QStringLiteral("org.test.disabled");

    // User has the plugin disabled before this scan
    pluginSettings()->registerPlugin(id, QStringLiteral("Disabled Plugin"), true);
    pluginSettings()->pluginEnabledFact(id)->setRawValue(false);

    QGCPluginManager manager;
    manager._processInspected({discoveredFixture(id, QStringLiteral("Disabled Plugin"))});

    QCOMPARE(manager._recordStore.records().size(), 1);
    const PluginLoadInfo& record = manager._recordStore.records().first();
    // Disabled, not Failed: activation was never attempted on the bogus path
    QCOMPARE(record.state, PluginState::Disabled);
    QVERIFY(record.plugin == nullptr);
    QVERIFY(manager.loadedPlugins().isEmpty());
}

void QGCPluginManagerTest::_incompatibleRecorded_test()
{
    const QString id = QStringLiteral("org.test.incompatible");

    PluginLoadInfo fixture = discoveredFixture(id, QStringLiteral("Incompatible Plugin"));
    fixture.state = PluginState::Incompatible;
    fixture.errorString = QStringLiteral("requires host version >= 9.9");

    QGCPluginManager manager;
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("requires host version"));
    manager._processInspected({fixture});
    verifyExpectedLogMessage();

    const PluginLoadInfo& record = manager._recordStore.records().first();
    QCOMPARE(record.state, PluginState::Incompatible);
    QCOMPARE(record.errorString, QStringLiteral("requires host version >= 9.9"));
    QVERIFY(record.plugin == nullptr);
    // Incompatible plugins are still registered so their setting persists
    QVERIFY(pluginSettings()->registeredPluginIds().contains(id));
}

void QGCPluginManagerTest::_settingsKeyedById_test()
{
    const QString id = QStringLiteral("org.test.alpha");
    const QString name = QStringLiteral("Alpha Display");

    QGCPluginManager manager;
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Failed to activate plugin"));
    manager._processInspected({discoveredFixture(id, name)});
    verifyExpectedLogMessage();

    // Fact is keyed by manifest id, not display name
    Fact* fact = pluginSettings()->pluginEnabledFact(id);
    QVERIFY(fact != nullptr);
    QVERIFY(pluginSettings()->pluginEnabledFact(name) == nullptr);
    QCOMPARE(fact->name(), id);

    // Display name comes from the record's manifest via the Fact label
    QCOMPARE(fact->label(), name);

    // Enabled by default, so activation was attempted and failed on the bogus path
    const PluginLoadInfo& record = manager._recordStore.records().first();
    QCOMPARE(record.state, PluginState::Failed);
    QVERIFY(!record.errorString.isEmpty());
    QVERIFY(record.plugin == nullptr);
}

void QGCPluginManagerTest::_failedWithoutIdNotRegistered_test()
{
    PluginLoadInfo fixture;
    fixture.filePath = QStringLiteral("/nonexistent/garbage.dylib");
    fixture.state = PluginState::Failed;
    fixture.errorString = QStringLiteral("no plugin metadata found");

    const QStringList idsBefore = pluginSettings()->registeredPluginIds();

    QGCPluginManager manager;
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("no plugin metadata found"));
    manager._processInspected({fixture});
    verifyExpectedLogMessage();

    // Record kept for display purposes, but no settings key without an id
    QCOMPARE(manager._recordStore.records().size(), 1);
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Failed);
    QCOMPARE(pluginSettings()->registeredPluginIds(), idsBefore);
}

void QGCPluginManagerTest::_duplicateIdFails_test()
{
    const QString id = QStringLiteral("org.test.duplicate");

    // Disabled so neither record attempts activation
    pluginSettings()->registerPlugin(id, QStringLiteral("Duplicate Plugin"), false);

    QGCPluginManager manager;
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("duplicate plugin id"));
    manager._processInspected({
        discoveredFixture(id, QStringLiteral("Duplicate Plugin")),
        discoveredFixture(id, QStringLiteral("Duplicate Plugin")),
    });
    verifyExpectedLogMessage();

    QCOMPARE(manager._recordStore.records().size(), 2);
    QCOMPARE(manager._recordStore.records()[0].state, PluginState::Disabled);
    QCOMPARE(manager._recordStore.records()[1].state, PluginState::Failed);
    QVERIFY(manager._recordStore.records()[1].errorString.contains(QStringLiteral("duplicate")));
}

void QGCPluginManagerTest::_setPluginEnabledPersistsAndReconciles_test()
{
    const QString id = QStringLiteral("org.test.toggle");

    pluginSettings()->registerPlugin(id, QStringLiteral("Toggle Plugin"), false);

    QGCPluginManager manager;
    manager._processInspected({discoveredFixture(id, QStringLiteral("Toggle Plugin"))});
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Disabled);

    // Enabling persists the Fact and attempts activation, which fails on the bogus path
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Failed to activate plugin"));
    manager.setPluginEnabled(id, true);
    verifyExpectedLogMessage();
    QVERIFY(pluginSettings()->isPluginEnabled(id));
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Failed);
    QVERIFY(!manager._recordStore.records().first().errorString.isEmpty());

    // Disabling persists the Fact; idempotent on a plugin that never activated
    manager.setPluginEnabled(id, false);
    QVERIFY(!pluginSettings()->isPluginEnabled(id));
    QVERIFY(manager._recordStore.records().first().plugin == nullptr);

    // Unknown id warns without side effects
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Plugin not found"));
    manager.setPluginEnabled(QStringLiteral("org.test.unknown"), true);
    verifyExpectedLogMessage();
    QCOMPARE(manager._recordStore.records().size(), 1);
}

void QGCPluginManagerTest::_reloadUnknownId_test()
{
    QGCPluginManager manager;
    manager._processInspected({});

    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Plugin not found for reload"));
    manager.reloadPlugin(QStringLiteral("org.test.nope"));
    verifyExpectedLogMessage();
    QVERIFY(manager._recordStore.records().isEmpty());
}

void QGCPluginManagerTest::_reloadKeepsIdentityOnFailedInspect_test()
{
    const QString id = QStringLiteral("org.test.reload");

    pluginSettings()->registerPlugin(id, QStringLiteral("Reload Plugin"), false);

    QGCPluginManager manager;
    manager._processInspected({discoveredFixture(id, QStringLiteral("Reload Plugin"))});
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Disabled);

    // Re-inspection of the bogus path fails; the record must keep its manifest id
    // so the settings key and id lookups stay coherent
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Reload inspection failed"));
    manager.reloadPlugin(id);
    verifyExpectedLogMessage();
    QCOMPARE(manager._recordStore.records().size(), 1);
    QCOMPARE(manager._recordStore.records().first().manifest.id, id);
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Failed);
    QVERIFY(!manager._recordStore.records().first().errorString.isEmpty());
    QVERIFY(manager._recordStore.find(id) != nullptr);
}

void QGCPluginManagerTest::_knownPluginsReflectsRecords_test()
{
    const QString activeId = QStringLiteral("org.test.active");
    const QString incompatibleId = QStringLiteral("org.test.knownincompatible");

    PluginLoadInfo activeFixture = discoveredFixture(activeId, QStringLiteral("Active Plugin"));
    activeFixture.state = PluginState::Active;
    activeFixture.manifest.description = QStringLiteral("Does things");

    PluginLoadInfo incompatibleFixture = discoveredFixture(incompatibleId, QStringLiteral("Incompatible Plugin"));
    incompatibleFixture.state = PluginState::Incompatible;
    incompatibleFixture.errorString = QStringLiteral("requires host version >= 9.9");

    QGCPluginManager manager;
    manager._recordStore.records() = {activeFixture, incompatibleFixture};

    const QVariantList known = manager.knownPlugins();
    QCOMPARE(known.size(), 2);

    const QVariantMap activeInfo = known[0].toMap();
    QCOMPARE(activeInfo["id"].toString(), activeId);
    QCOMPARE(activeInfo["name"].toString(), QStringLiteral("Active Plugin"));
    QCOMPARE(activeInfo["version"].toString(), QStringLiteral("1.0.0"));
    QCOMPARE(activeInfo["vendor"].toString(), QStringLiteral("Test Org"));
    QCOMPARE(activeInfo["description"].toString(), QStringLiteral("Does things"));
    QCOMPARE(activeInfo["state"].toString(), QStringLiteral("Active"));
    QCOMPARE(activeInfo["statusText"].toString(), QStringLiteral("Active"));

    const QVariantMap incompatibleInfo = known[1].toMap();
    QCOMPARE(incompatibleInfo["state"].toString(), QStringLiteral("Incompatible"));
    QVERIFY(incompatibleInfo["statusText"].toString().contains(QStringLiteral("requires host version >= 9.9")));
}

void QGCPluginManagerTest::_contributionsAddedAndRemoved_test()
{
    const QString id = QStringLiteral("org.test.contributor");

    PluginLoadInfo record = discoveredFixture(id, QStringLiteral("Contributor"));
    QJsonObject toolMenu;
    toolMenu[QStringLiteral("title")]  = QStringLiteral("Contributor");
    toolMenu[QStringLiteral("source")] = QStringLiteral("qrc:/qml/ContributorView.qml");
    QJsonObject flyViewPanel;
    flyViewPanel[QStringLiteral("panel")] = QStringLiteral("qrc:/qml/ContributorFlyPanel.qml");
    QJsonObject contributes;
    contributes[QStringLiteral("toolMenu")]     = toolMenu;
    contributes[QStringLiteral("flyViewPanel")] = flyViewPanel;
    record.manifest.contributes = contributes;

    QString error;
    record.contributions = PluginContributions::fromManifest(record.manifest, QString(), &error);
    QVERIFY(error.isEmpty());

    QGCPluginManager manager;
    manager._addContributions(record);

    QCOMPARE(manager.toolMenuItems().size(), 1);
    QCOMPARE(manager.toolMenuItems().first().toMap()["pluginId"].toString(), id);
    QCOMPARE(manager.flyViewPanelItems().size(), 1);
    QCOMPARE(manager.flyViewPanelItems().first().toMap()["panelUrl"].toString(), QStringLiteral("qrc:/qml/ContributorFlyPanel.qml"));
    QVERIFY(manager.planViewPanelItems().isEmpty());

    manager._removeContributionsForPlugin(id);
    QVERIFY(manager.toolMenuItems().isEmpty());
    QVERIFY(manager.flyViewPanelItems().isEmpty());
}

void QGCPluginManagerTest::_loggingControllerFromManifest_test()
{
    const QString id = QStringLiteral("org.test.logger");

    // The claim is manifest data: no plugin instance exists, only a record state
    PluginLoadInfo record = discoveredFixture(id, QStringLiteral("Logger"));
    record.state = PluginState::Active;
    record.contributions.controlsTelemetryLogging = true;

    QGCPluginManager manager;
    manager._recordStore.records() = {record};
    manager._recalcLoggingController();
    QVERIFY(manager.hasLoggingController());

    // The claim only counts while the plugin is active
    manager._recordStore.records().first().state = PluginState::Disabled;
    manager._recalcLoggingController();
    QVERIFY(!manager.hasLoggingController());
}

void QGCPluginManagerTest::_notifyEpilogueEmitsOnce_test()
{
    const QString id = QStringLiteral("org.test.notifyepilogue");

    // Registered disabled so the first scan below records it as Disabled rather
    // than attempting (and failing) activation, keeping it in the
    // Disabled/Discovered class that setPluginEnabled(true) below can re-activate.
    pluginSettings()->registerPlugin(id, QStringLiteral("Notify Plugin"), false);

    QGCPluginManager manager;
    MultiSignalSpy spy;
    QVERIFY(spy.init(&manager, {"loadedPluginsChanged", "replayExtensionChanged"}));

    // Discovery: disabled, so no activation attempt — still exactly one epilogue run.
    manager._processInspected({discoveredFixture(id, QStringLiteral("Notify Plugin"))});
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Disabled);
    QVERIFY_SIGNAL_COUNT(spy, "loadedPluginsChanged", 1);
    QVERIFY_NO_SIGNAL(spy, "replayExtensionChanged");
    spy.clearAllSignals();

    // Enable: one activation attempt (fails on the bogus path), one epilogue run.
    // No plugin ever loads, so the replay extension never actually changes.
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Failed to activate plugin"));
    manager.setPluginEnabled(id, true);
    verifyExpectedLogMessage();
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Failed);
    QVERIFY_SIGNAL_COUNT(spy, "loadedPluginsChanged", 1);
    QVERIFY_NO_SIGNAL(spy, "replayExtensionChanged");
    spy.clearAllSignals();

    // Disable: state is Failed, not Active, so _deactivateRecord is never reached
    // and no epilogue runs at all here — setPluginEnabled(false) has no matching
    // branch for Failed.
    manager.setPluginEnabled(id, false);
    QVERIFY_NO_SIGNAL(spy, "loadedPluginsChanged");
    spy.clearAllSignals();

    // Reload: re-inspection of the same bogus path fails identically; exactly one
    // epilogue run despite the failure.
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Reload inspection failed"));
    manager.reloadPlugin(id);
    verifyExpectedLogMessage();
    QVERIFY_SIGNAL_COUNT(spy, "loadedPluginsChanged", 1);
    QVERIFY_NO_SIGNAL(spy, "replayExtensionChanged");
    QCOMPARE(manager._recordStore.records().first().manifest.id, id);
}

void QGCPluginManagerTest::_notifyEpilogueOwnsReplayExtension_test()
{
    // Two Active records, both declaring a replay extension via a real plugin
    // instance — _recalcReplayExtension() is the only writer of _replayExtension
    // now (Pillar 2), and must pick in _records list order: first provider wins.
    QGCPluginManager manager;
    manager._recordStore.records() = {
        activeReplayProviderFixture(QStringLiteral("org.test.replayfirst"), &manager),
        activeReplayProviderFixture(QStringLiteral("org.test.replaysecond"), &manager),
    };
    QGCReplayExtension* const firstExt = manager._recordStore.records()[0].plugin->replayExtension();
    QGCReplayExtension* const secondExt = manager._recordStore.records()[1].plugin->replayExtension();

    MultiSignalSpy spy;
    QVERIFY(spy.init(&manager, {"replayExtensionChanged"}));

    // First recalc: nothing registered yet, list order picks index 0.
    manager._notifyRecordsChanged();
    QCOMPARE(manager.replayExtension(), firstExt);
    QVERIFY_SIGNAL_COUNT(spy, "replayExtensionChanged", 1);
    spy.clearAllSignals();

    // Idempotent: re-running with the same records is a no-op, no re-emit.
    manager._notifyRecordsChanged();
    QCOMPARE(manager.replayExtension(), firstExt);
    QVERIFY_NO_SIGNAL(spy, "replayExtensionChanged");

    // Deactivating the first provider (list order still [first, second], but
    // first no longer qualifies — _deactivateRecord() always nulls record.plugin,
    // which is what _recalcReplayExtension() actually keys on) hands ownership to
    // the second — exactly one emission. This pins the recalc's own selection and
    // idempotence directly; it does not drive setPluginEnabled()/reloadPlugin(),
    // so it does not reproduce the specific double-emit the review found in
    // _activateRecord() — that path needs a real loadable-plugin fixture to cover
    // end-to-end (see TestPlugin/), left for a follow-up rather than U1.
    manager._recordStore.records()[0].state = PluginState::Disabled;
    manager._recordStore.records()[0].plugin = nullptr;
    manager._notifyRecordsChanged();
    QCOMPARE(manager.replayExtension(), secondExt);
    QVERIFY_SIGNAL_COUNT(spy, "replayExtensionChanged", 1);
}

void QGCPluginManagerTest::_lateActivationResolvesPluginQml_test()
{
    // The regression guard for "enable a plugin without restarting and its QML
    // renders blank, silently". Every other test here activates plugins with no QML
    // engine in the process, which is exactly why none of them ever saw this.
    //
    // Pre-fix this fails with "File name case mismatch", a message Qt only produces
    // on macOS and Windows — the stale directory listing itself is platform-neutral,
    // but do not assume this slot has been seen to fail anywhere else.
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));  // what createQmlApplicationEngine() does

    // Host startup: loading QML out of qrc:/qml makes the engine enumerate that
    // directory, and the type loader caches the listing. Anything registered into it
    // afterwards is invisible to the engine unless the cache is invalidated — even
    // though QFile::exists() finds it.
    QQmlComponent hostComponent(&engine, QUrl(QStringLiteral("qrc:/qml/PluginActivationHostProbe.qml")));
    QVERIFY2(hostComponent.isReady(), qPrintable(hostComponent.errorString()));
    QScopedPointer<QObject> hostObject(hostComponent.create());
    QVERIFY(hostObject);

    QGCPluginManager manager;
    manager.setQmlEngine(&engine);

    // Resolve the panel from inside the contribution signal, because that is when the
    // QML side does it: the Repeater/Loader bound to flyViewPanelItems fetches the new
    // url synchronously within this emission. An invalidation that ran after
    // _addContributions() would still pass a check made after activation returns.
    bool signalled = false;
    bool resolvedDuringEmit = false;
    QString resolveError;
    const QMetaObject::Connection connection =
        QObject::connect(&manager, &QGCPluginManager::flyViewPanelItemsChanged, &manager, [&]() {
            signalled = true;
            QQmlComponent panel(&engine, QUrl(manager.flyViewPanelItems().constFirst().toMap()[QStringLiteral("panelUrl")].toString()));
            resolvedDuringEmit = panel.isReady();
            resolveError = panel.errorString();
        });

    // Positive control: the whole test is only meaningful if the fixture's resources
    // are registered *after* the engine enumerated the directory above. If some
    // earlier slot ever loads the fixture first, the cache is never stale and this
    // test would pass for the wrong reason, silently and forever.
    QVERIFY2(!QFile::exists(QStringLiteral(":/qml/TestPluginFixturePanel.qml")),
             "fixture resources were already registered — this test no longer reproduces late registration");

    PluginLoadInfo record = QGCPluginLoader::inspect(testPluginFixturePath());
    QCOMPARE(record.state, PluginState::Discovered);
    QCOMPARE(record.contributions.flyViewPanelItem[QStringLiteral("panelUrl")].toString(),
             QStringLiteral("qrc:/qml/TestPluginFixturePanel.qml"));

    manager._activateRecord(record);
    // The other half of the control: activation dlopened the library, so the file is
    // there now. The bug is that the engine cannot see it, not that it is missing.
    QVERIFY(QFile::exists(QStringLiteral(":/qml/TestPluginFixturePanel.qml")));
    // Load-bearing, not tidiness: the manager's destructor clears the panel list and
    // then re-emits, and the handler would call constFirst() on an empty list.
    QObject::disconnect(connection);
    QCOMPARE(record.state, PluginState::Active);

    QVERIFY(signalled);
    QVERIFY2(resolvedDuringEmit, qPrintable(QStringLiteral("plugin QML did not resolve on activation: %1").arg(resolveError)));

    // Invalidating the cache must not cost the host the QML it already has running:
    // a live object's bindings have to keep re-evaluating.
    hostObject->setProperty("scale2", 7.0);
    QCOMPARE(hostObject->property("derived").toDouble(), 14.0);

    delete record.plugin;
}

void QGCPluginManagerTest::_userDirPluginNeedsApprovalFirstSight_test()
{
    const QString id = QStringLiteral("org.test.userdirnew");
    const QString packageDir = _writePackage(PluginInstaller::userPluginsDir(), id, QStringLiteral("qml"));
    QVERIFY(!packageDir.isEmpty());

    QGCPluginManager manager;
    manager._processInspected({QGCPluginLoader::inspectPackage(packageDir)});

    // Enabled by default — it is the consent gate, not the enabled Fact, that blocks it
    QVERIFY(pluginSettings()->isPluginEnabled(id));
    QCOMPARE(manager._recordStore.records().size(), 1);
    QCOMPARE(manager._recordStore.records().first().state, PluginState::NeedsApproval);
    QVERIFY(manager._recordStore.records().first().plugin == nullptr);
    QVERIFY(manager.loadedPlugins().isEmpty());
    QVERIFY(pluginSettings()->approvedPluginDigest(id).isEmpty());
}

void QGCPluginManagerTest::_approvalActivatesAndPersists_test()
{
    const QString id = QStringLiteral("org.test.userdirapprove");
    const QString packageDir = _writePackage(PluginInstaller::userPluginsDir(), id, QStringLiteral("qml"));
    QVERIFY(!packageDir.isEmpty());

    QGCPluginManager manager;
    manager._processInspected({QGCPluginLoader::inspectPackage(packageDir)});
    QCOMPARE(manager._recordStore.records().first().state, PluginState::NeedsApproval);

    // Approval records the consent digest and activates (qml tier: trivially)
    manager.approvePlugin(id);
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Active);
    QVERIFY(!pluginSettings()->approvedPluginDigest(id).isEmpty());

    // "Restart": a fresh manager scanning unchanged content activates without re-prompting
    QGCPluginManager restarted;
    restarted._processInspected({QGCPluginLoader::inspectPackage(packageDir)});
    QCOMPARE(restarted._recordStore.records().first().state, PluginState::Active);

    // Removal revokes consent — identical content arriving later starts unapproved
    QVERIFY2(restarted.removePlugin(id).isEmpty(), "removePlugin failed");
    QVERIFY(pluginSettings()->approvedPluginDigest(id).isEmpty());
}

void QGCPluginManagerTest::_changedContentReprompts_test()
{
    const QString id = QStringLiteral("org.test.userdirchanged");
    QString packageDir = _writePackage(PluginInstaller::userPluginsDir(), id, QStringLiteral("qml"));
    QVERIFY(!packageDir.isEmpty());

    QGCPluginManager manager;
    manager._processInspected({QGCPluginLoader::inspectPackage(packageDir)});
    manager.approvePlugin(id);
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Active);

    // The manifest changes on disk after approval: the recorded digest no longer vouches
    packageDir = _writePackage(PluginInstaller::userPluginsDir(), id, QStringLiteral("qml"), QStringLiteral("Changed content"));
    QVERIFY(!packageDir.isEmpty());

    QGCPluginManager restarted;
    restarted._processInspected({QGCPluginLoader::inspectPackage(packageDir)});
    QCOMPARE(restarted._recordStore.records().first().state, PluginState::NeedsApproval);
    QVERIFY2(restarted._recordStore.records().first().errorString.contains(QStringLiteral("changed")),
             qPrintable(restarted._recordStore.records().first().errorString));
}

void QGCPluginManagerTest::_bundleDirPluginTrusted_test()
{
    const QString id = QStringLiteral("org.test.bundledir");
    const QString packageDir = _writePackage(tempPath(QStringLiteral("bundle-plugins")), id, QStringLiteral("qml"));
    QVERIFY(!packageDir.isEmpty());

    // Trusted class: activates on first sight, and no consent digest is ever recorded
    QGCPluginManager manager;
    manager._processInspected({QGCPluginLoader::inspectPackage(packageDir)});
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Active);
    QVERIFY(pluginSettings()->approvedPluginDigest(id).isEmpty());
}

void QGCPluginManagerTest::_crashSentinelQuarantines_test()
{
    const QString id = QStringLiteral("org.test.crashed");

    // The previous run died inside this plugin's activation: its sentinel lingers
    QSettings settings;
    settings.setValue(QString::fromLatin1(kLoadingPluginIdKey), id);

    QGCPluginManager manager;
    expectLogMessage("PluginSystem.PluginRecordStore", QtWarningMsg, QRegularExpression("Previous run crashed while loading plugin"));
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("QGC crashed while loading this plugin last run"));
    manager._recordStore.checkCrashSentinel();
    manager._processInspected({discoveredFixture(id, QStringLiteral("Crashed Plugin"))});
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();

    // Quarantined with no activation attempt (an attempt on the bogus path would
    // have flipped the state to Failed), despite the enabled-by-default Fact
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Quarantined);
    QVERIFY(manager._recordStore.records().first().plugin == nullptr);
    QVERIFY(manager.loadedPlugins().isEmpty());

    // The lingering id was promoted to the persistent marker
    settings.sync();
    QVERIFY(settings.value(QString::fromLatin1(kLoadingPluginIdKey)).toString().isEmpty());
    QCOMPARE(settings.value(QString::fromLatin1(kCrashedPluginIdKey)).toString(), id);

    // Re-enable clears the marker and retries: activation is now attempted and
    // fails on the bogus path — proving the gate opened
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Failed to activate plugin"));
    manager.setPluginEnabled(id, true);
    verifyExpectedLogMessage();
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Failed);
    settings.sync();
    QVERIFY(settings.value(QString::fromLatin1(kCrashedPluginIdKey)).toString().isEmpty());
}

void QGCPluginManagerTest::_crashSentinelOutranksConsent_test()
{
    const QString id = QStringLiteral("org.test.crashedunapproved");
    const QString packageDir = _writePackage(PluginInstaller::userPluginsDir(), id, QStringLiteral("qml"));
    QVERIFY(!packageDir.isEmpty());

    QSettings settings;
    settings.setValue(QString::fromLatin1(kLoadingPluginIdKey), id);

    QGCPluginManager manager;
    expectLogMessage("PluginSystem.PluginRecordStore", QtWarningMsg, QRegularExpression("Previous run crashed while loading plugin"));
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("QGC crashed while loading this plugin last run"));
    manager._recordStore.checkCrashSentinel();
    manager._processInspected({QGCPluginLoader::inspectPackage(packageDir)});
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();

    // Crash quarantine outranks the consent gate: a plugin that crashed the host
    // must not be runnable by mere approval
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Quarantined);

    // Re-enable clears the crash marker but not the consent requirement
    manager.setPluginEnabled(id, true);
    QCOMPARE(manager._recordStore.records().first().state, PluginState::NeedsApproval);
    QVERIFY(manager.loadedPlugins().isEmpty());
    settings.sync();
    QVERIFY(settings.value(QString::fromLatin1(kCrashedPluginIdKey)).toString().isEmpty());
}

void QGCPluginManagerTest::_sentinelClearedAfterActivation_test()
{
    const QString id = QStringLiteral("org.test.sentinelclear");

    QGCPluginManager manager;
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Failed to activate plugin"));
    manager._processInspected({discoveredFixture(id, QStringLiteral("Sentinel Plugin"))});
    verifyExpectedLogMessage();

    // Activation was attempted (and failed on the bogus path); a completed attempt
    // — even a failed one — must leave no lingering sentinel to blame next boot
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Failed);
    QSettings settings;
    settings.sync();
    QVERIFY(settings.value(QString::fromLatin1(kLoadingPluginIdKey)).toString().isEmpty());
    QVERIFY(settings.value(QString::fromLatin1(kCrashedPluginIdKey)).toString().isEmpty());
}

#if defined(Q_OS_MACOS)
void QGCPluginManagerTest::_quarantinedBinaryGated_test()
{
    const QString id = QStringLiteral("org.test.quarantinedbin");

    // Disabled so no code path ever tries to load the placeholder binary
    pluginSettings()->registerPlugin(id, QStringLiteral("Quarantined Binary"), false);
    pluginSettings()->pluginEnabledFact(id)->setRawValue(false);

    const QString packageDir = _writePackage(PluginInstaller::userPluginsDir(), id, QStringLiteral("sdk"));
    QVERIFY(!packageDir.isEmpty());

    QGCPluginManager manager;
    manager._processInspected({QGCPluginLoader::inspectPackage(packageDir)});
    QCOMPARE(manager._recordStore.records().first().state, PluginState::NeedsApproval);
    manager.approvePlugin(id);
    QCOMPARE(manager._recordStore.records().first().state, PluginState::Disabled);

    // The D15 partial case: manifest stays clean while a freshly-downloaded (quarantined)
    // binary is swapped in. Content is unchanged, so the consent digest still matches —
    // only the widened manifest+binary quarantine check can catch this.
    const QString binaryPath = packageDir + QStringLiteral("/bin/macos-universal/libfixture.dylib");
    const QByteArray quarantineValue = QByteArrayLiteral("0081;00000000;QGCTest;");
    QCOMPARE(setxattr(binaryPath.toUtf8().constData(), "com.apple.quarantine",
                      quarantineValue.constData(), quarantineValue.size(), 0, 0), 0);

    QGCPluginManager restarted;
    restarted._processInspected({QGCPluginLoader::inspectPackage(packageDir)});
    QCOMPARE(restarted._recordStore.records().first().state, PluginState::NeedsApproval);
    QVERIFY2(restarted._recordStore.records().first().errorString.contains(QStringLiteral("Downloaded")),
             qPrintable(restarted._recordStore.records().first().errorString));
}
#endif

UT_REGISTER_TEST(QGCPluginManagerTest, TestLabel::Unit)
