#include "QGCPluginManagerTest.h"

#include <QtCore/QJsonObject>

#include "Fact.h"
#include "PluginContributions.h"
#include "PluginSettings.h"
#include "QGCPluginInterface.h"
#include "QGCPluginManager.h"
#include "SettingsManager.h"

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
    info.manifest.tier = PluginManifest::Tier::Internal;
    info.manifest.apiVersion = QGCPluginApiVersion;
    return info;
}

PluginSettings* pluginSettings()
{
    return SettingsManager::instance()->pluginSettings();
}

} // namespace

void QGCPluginManagerTest::_disabledNeverActivated_test()
{
    const QString id = QStringLiteral("org.test.disabled");

    // User has the plugin disabled before this scan
    pluginSettings()->registerPlugin(id, QStringLiteral("Disabled Plugin"), true);
    pluginSettings()->pluginEnabledFact(id)->setRawValue(false);

    QGCPluginManager manager;
    manager._processInspected({discoveredFixture(id, QStringLiteral("Disabled Plugin"))});

    QCOMPARE(manager._records.size(), 1);
    const PluginLoadInfo& record = manager._records.first();
    // Disabled, not Failed: activation was never attempted on the bogus path
    QCOMPARE(record.state, PluginState::Disabled);
    QVERIFY(record.plugin == nullptr);
    QVERIFY(manager.loadedPlugins().isEmpty());
}

void QGCPluginManagerTest::_defaultDisabledById_test()
{
    const QString exampleId = QStringLiteral("org.qgroundcontrol.example");

    QGCPluginManager manager;
    manager._processInspected({discoveredFixture(exampleId, QStringLiteral("Renamed Example"))});

    // The Example plugin defaults to disabled, keyed by manifest id — a display-name
    // change must not affect it
    QVERIFY(!pluginSettings()->isPluginEnabled(exampleId));
    QCOMPARE(manager._records.first().state, PluginState::Disabled);
    QVERIFY(manager._records.first().plugin == nullptr);

    // A different plugin merely NAMED "Example" defaults to enabled
    const QString otherId = QStringLiteral("org.test.namedexample");
    manager._processInspected({discoveredFixture(otherId, QStringLiteral("Example"))});
    QVERIFY(pluginSettings()->isPluginEnabled(otherId));
}

void QGCPluginManagerTest::_incompatibleRecorded_test()
{
    const QString id = QStringLiteral("org.test.incompatible");

    PluginLoadInfo fixture = discoveredFixture(id, QStringLiteral("Incompatible Plugin"));
    fixture.state = PluginState::Incompatible;
    fixture.errorString = QStringLiteral("requires host version >= 9.9");

    QGCPluginManager manager;
    manager._processInspected({fixture});

    const PluginLoadInfo& record = manager._records.first();
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
    manager._processInspected({discoveredFixture(id, name)});

    // Fact is keyed by manifest id, not display name
    Fact* fact = pluginSettings()->pluginEnabledFact(id);
    QVERIFY(fact != nullptr);
    QVERIFY(pluginSettings()->pluginEnabledFact(name) == nullptr);
    QCOMPARE(fact->name(), id);

    // Display name comes from the record's manifest via the Fact label
    QCOMPARE(fact->label(), name);

    // Enabled by default, so activation was attempted and failed on the bogus path
    const PluginLoadInfo& record = manager._records.first();
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
    manager._processInspected({fixture});

    // Record kept for display purposes, but no settings key without an id
    QCOMPARE(manager._records.size(), 1);
    QCOMPARE(manager._records.first().state, PluginState::Failed);
    QCOMPARE(pluginSettings()->registeredPluginIds(), idsBefore);
}

void QGCPluginManagerTest::_duplicateIdFails_test()
{
    const QString id = QStringLiteral("org.test.duplicate");

    // Disabled so neither record attempts activation
    pluginSettings()->registerPlugin(id, QStringLiteral("Duplicate Plugin"), false);

    QGCPluginManager manager;
    manager._processInspected({
        discoveredFixture(id, QStringLiteral("Duplicate Plugin")),
        discoveredFixture(id, QStringLiteral("Duplicate Plugin")),
    });

    QCOMPARE(manager._records.size(), 2);
    QCOMPARE(manager._records[0].state, PluginState::Disabled);
    QCOMPARE(manager._records[1].state, PluginState::Failed);
    QVERIFY(manager._records[1].errorString.contains(QStringLiteral("duplicate")));
}

void QGCPluginManagerTest::_setPluginEnabledPersistsAndReconciles_test()
{
    const QString id = QStringLiteral("org.test.toggle");

    pluginSettings()->registerPlugin(id, QStringLiteral("Toggle Plugin"), false);

    QGCPluginManager manager;
    manager._processInspected({discoveredFixture(id, QStringLiteral("Toggle Plugin"))});
    QCOMPARE(manager._records.first().state, PluginState::Disabled);

    // Enabling persists the Fact and attempts activation, which fails on the bogus path
    manager.setPluginEnabled(id, true);
    QVERIFY(pluginSettings()->isPluginEnabled(id));
    QCOMPARE(manager._records.first().state, PluginState::Failed);
    QVERIFY(!manager._records.first().errorString.isEmpty());

    // Disabling persists the Fact; idempotent on a plugin that never activated
    manager.setPluginEnabled(id, false);
    QVERIFY(!pluginSettings()->isPluginEnabled(id));
    QVERIFY(manager._records.first().plugin == nullptr);

    // Unknown id warns without side effects
    manager.setPluginEnabled(QStringLiteral("org.test.unknown"), true);
    QCOMPARE(manager._records.size(), 1);
}

void QGCPluginManagerTest::_reloadUnknownId_test()
{
    QGCPluginManager manager;
    manager._processInspected({});

    manager.reloadPlugin(QStringLiteral("org.test.nope"));
    QVERIFY(manager._records.isEmpty());
}

void QGCPluginManagerTest::_reloadKeepsIdentityOnFailedInspect_test()
{
    const QString id = QStringLiteral("org.test.reload");

    pluginSettings()->registerPlugin(id, QStringLiteral("Reload Plugin"), false);

    QGCPluginManager manager;
    manager._processInspected({discoveredFixture(id, QStringLiteral("Reload Plugin"))});
    QCOMPARE(manager._records.first().state, PluginState::Disabled);

    // Re-inspection of the bogus path fails; the record must keep its manifest id
    // so the settings key and id lookups stay coherent
    manager.reloadPlugin(id);
    QCOMPARE(manager._records.size(), 1);
    QCOMPARE(manager._records.first().manifest.id, id);
    QCOMPARE(manager._records.first().state, PluginState::Failed);
    QVERIFY(!manager._records.first().errorString.isEmpty());
    QVERIFY(manager._findRecord(id) != nullptr);
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
    manager._records = {activeFixture, incompatibleFixture};

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
    record.contributions = PluginContributions::fromManifest(record.manifest, &error);
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
    manager._records = {record};
    manager._recalcLoggingController();
    QVERIFY(manager.hasLoggingController());

    // The claim only counts while the plugin is active
    manager._records.first().state = PluginState::Disabled;
    manager._recalcLoggingController();
    QVERIFY(!manager.hasLoggingController());
}

UT_REGISTER_TEST(QGCPluginManagerTest, TestLabel::Unit)
