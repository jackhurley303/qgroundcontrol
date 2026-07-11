#include "PluginContributionsTest.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

#include "PluginContributions.h"
#include "PluginManifest.h"

namespace {

PluginManifest manifestWithContributes(const QJsonObject& contributes)
{
    PluginManifest manifest;
    manifest.id = QStringLiteral("org.test.contrib");
    manifest.name = QStringLiteral("Contrib Plugin");
    manifest.contributes = contributes;
    return manifest;
}

QJsonObject fullContributes()
{
    QJsonObject toolMenu;
    toolMenu[QStringLiteral("title")]         = QStringLiteral("Contrib");
    toolMenu[QStringLiteral("icon")]          = QStringLiteral("/qmlimages/plugin.svg");
    toolMenu[QStringLiteral("source")]        = QStringLiteral("qrc:/qml/ContribView.qml");
    toolMenu[QStringLiteral("toolbarSource")] = QStringLiteral("qrc:/qml/ContribToolBar.qml");

    QJsonObject flyViewPanel;
    flyViewPanel[QStringLiteral("panel")]           = QStringLiteral("qrc:/qml/ContribFlyPanel.qml");
    flyViewPanel[QStringLiteral("dock")]            = QStringLiteral("qrc:/qml/ContribFlyDock.qml");
    flyViewPanel[QStringLiteral("defaultWidth")]    = 35;
    flyViewPanel[QStringLiteral("defaultHeight")]   = 18;
    flyViewPanel[QStringLiteral("defaultPosition")] = QJsonArray{0.0, 0.5};

    QJsonObject planViewPanel;
    planViewPanel[QStringLiteral("panel")]           = QStringLiteral("qrc:/qml/ContribPlanPanel.qml");
    planViewPanel[QStringLiteral("dock")]            = QStringLiteral("qrc:/qml/ContribPlanDock.qml");
    planViewPanel[QStringLiteral("defaultWidth")]    = 30;
    planViewPanel[QStringLiteral("defaultHeight")]   = 14;
    planViewPanel[QStringLiteral("defaultPosition")] = QJsonArray{1.0, 0.0};

    QJsonObject contributes;
    contributes[QStringLiteral("toolMenu")]         = toolMenu;
    contributes[QStringLiteral("flyViewPanel")]     = flyViewPanel;
    contributes[QStringLiteral("planViewPanel")]    = planViewPanel;
    contributes[QStringLiteral("replay")]           = true;
    contributes[QStringLiteral("telemetryLogging")] = true;
    return contributes;
}

} // namespace

void PluginContributionsTest::_fullContributes_test()
{
    QString error;
    const PluginContributions contributions =
        PluginContributions::fromManifest(manifestWithContributes(fullContributes()), &error);
    QVERIFY(error.isEmpty());

    // Whole-map comparisons pin the exact shapes the QML consumers read
    QVariantMap expectedToolMenu;
    expectedToolMenu[QStringLiteral("pluginId")]      = QStringLiteral("org.test.contrib");
    expectedToolMenu[QStringLiteral("title")]         = QStringLiteral("Contrib");
    expectedToolMenu[QStringLiteral("icon")]          = QStringLiteral("/qmlimages/plugin.svg");
    expectedToolMenu[QStringLiteral("source")]        = QStringLiteral("qrc:/qml/ContribView.qml");
    expectedToolMenu[QStringLiteral("toolbarSource")] = QStringLiteral("qrc:/qml/ContribToolBar.qml");
    QCOMPARE(contributions.toolMenuItem, expectedToolMenu);

    QVariantMap expectedFlyPanel;
    expectedFlyPanel[QStringLiteral("pluginId")]         = QStringLiteral("org.test.contrib");
    expectedFlyPanel[QStringLiteral("name")]             = QStringLiteral("Contrib Plugin");
    expectedFlyPanel[QStringLiteral("panelUrl")]         = QStringLiteral("qrc:/qml/ContribFlyPanel.qml");
    expectedFlyPanel[QStringLiteral("dockUrl")]          = QStringLiteral("qrc:/qml/ContribFlyDock.qml");
    expectedFlyPanel[QStringLiteral("defaultWidth")]     = 35.0;
    expectedFlyPanel[QStringLiteral("defaultHeight")]    = 18.0;
    expectedFlyPanel[QStringLiteral("defaultXFraction")] = 0.0;
    expectedFlyPanel[QStringLiteral("defaultYFraction")] = 0.5;
    QCOMPARE(contributions.flyViewPanelItem, expectedFlyPanel);

    QVariantMap expectedPlanPanel;
    expectedPlanPanel[QStringLiteral("pluginId")]         = QStringLiteral("org.test.contrib");
    expectedPlanPanel[QStringLiteral("name")]             = QStringLiteral("Contrib Plugin");
    expectedPlanPanel[QStringLiteral("panelUrl")]         = QStringLiteral("qrc:/qml/ContribPlanPanel.qml");
    expectedPlanPanel[QStringLiteral("dockUrl")]          = QStringLiteral("qrc:/qml/ContribPlanDock.qml");
    expectedPlanPanel[QStringLiteral("defaultWidth")]     = 30.0;
    expectedPlanPanel[QStringLiteral("defaultHeight")]    = 14.0;
    expectedPlanPanel[QStringLiteral("defaultXFraction")] = 1.0;
    expectedPlanPanel[QStringLiteral("defaultYFraction")] = 0.0;
    QCOMPARE(contributions.planViewPanelItem, expectedPlanPanel);

    QVERIFY(contributions.providesReplayExtension);
    QVERIFY(contributions.controlsTelemetryLogging);
}

void PluginContributionsTest::_emptyContributes_test()
{
    QString error;
    const PluginContributions contributions =
        PluginContributions::fromManifest(manifestWithContributes(QJsonObject()), &error);
    QVERIFY(error.isEmpty());
    QVERIFY(contributions.toolMenuItem.isEmpty());
    QVERIFY(contributions.flyViewPanelItem.isEmpty());
    QVERIFY(contributions.planViewPanelItem.isEmpty());
    QVERIFY(!contributions.providesReplayExtension);
    QVERIFY(!contributions.controlsTelemetryLogging);
}

void PluginContributionsTest::_panelDefaults_test()
{
    // Panel with only the required key: optional fields take the legacy
    // framework-default sentinels (0 sizes, -1 fractions, empty dock)
    QJsonObject panel;
    panel[QStringLiteral("panel")] = QStringLiteral("qrc:/qml/MinimalPanel.qml");
    QJsonObject contributes;
    contributes[QStringLiteral("flyViewPanel")] = panel;

    QString error;
    const PluginContributions contributions =
        PluginContributions::fromManifest(manifestWithContributes(contributes), &error);
    QVERIFY(error.isEmpty());

    const QVariantMap item = contributions.flyViewPanelItem;
    QCOMPARE(item[QStringLiteral("panelUrl")].toString(), QStringLiteral("qrc:/qml/MinimalPanel.qml"));
    QCOMPARE(item[QStringLiteral("dockUrl")].toString(), QString());
    QCOMPARE(item[QStringLiteral("defaultWidth")].toDouble(), 0.0);
    QCOMPARE(item[QStringLiteral("defaultHeight")].toDouble(), 0.0);
    QCOMPARE(item[QStringLiteral("defaultXFraction")].toDouble(), -1.0);
    QCOMPARE(item[QStringLiteral("defaultYFraction")].toDouble(), -1.0);
}

void PluginContributionsTest::_missingToolMenuRequired_test()
{
    QJsonObject toolMenu;
    toolMenu[QStringLiteral("title")] = QStringLiteral("No Source");
    QJsonObject contributes;
    contributes[QStringLiteral("toolMenu")] = toolMenu;

    QString error;
    const PluginContributions contributions =
        PluginContributions::fromManifest(manifestWithContributes(contributes), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("source")));
    QVERIFY(contributions.toolMenuItem.isEmpty());
}

void PluginContributionsTest::_missingPanelRequired_test()
{
    QJsonObject panel;
    panel[QStringLiteral("dock")] = QStringLiteral("qrc:/qml/DockOnly.qml");
    QJsonObject contributes;
    contributes[QStringLiteral("planViewPanel")] = panel;

    QString error;
    const PluginContributions contributions =
        PluginContributions::fromManifest(manifestWithContributes(contributes), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("panel")));
    QVERIFY(contributions.planViewPanelItem.isEmpty());
}

void PluginContributionsTest::_wrongTypes_test()
{
    QString error;

    // toolMenu must be an object
    QJsonObject contributes;
    contributes[QStringLiteral("toolMenu")] = QStringLiteral("not an object");
    QVERIFY(PluginContributions::fromManifest(manifestWithContributes(contributes), &error).toolMenuItem.isEmpty());
    QVERIFY(!error.isEmpty());

    // defaultWidth must be a number
    error.clear();
    QJsonObject panel;
    panel[QStringLiteral("panel")]        = QStringLiteral("qrc:/qml/Panel.qml");
    panel[QStringLiteral("defaultWidth")] = QStringLiteral("35");
    contributes = QJsonObject();
    contributes[QStringLiteral("flyViewPanel")] = panel;
    QVERIFY(PluginContributions::fromManifest(manifestWithContributes(contributes), &error).flyViewPanelItem.isEmpty());
    QVERIFY(!error.isEmpty());

    // replay must be a boolean
    error.clear();
    contributes = QJsonObject();
    contributes[QStringLiteral("replay")] = QStringLiteral("yes");
    QVERIFY(!PluginContributions::fromManifest(manifestWithContributes(contributes), &error).providesReplayExtension);
    QVERIFY(!error.isEmpty());
}

void PluginContributionsTest::_badDefaultPosition_test()
{
    QString error;

    QJsonObject panel;
    panel[QStringLiteral("panel")]           = QStringLiteral("qrc:/qml/Panel.qml");
    panel[QStringLiteral("defaultPosition")] = QJsonArray{0.0, 0.5, 1.0};
    QJsonObject contributes;
    contributes[QStringLiteral("flyViewPanel")] = panel;
    QVERIFY(PluginContributions::fromManifest(manifestWithContributes(contributes), &error).flyViewPanelItem.isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    panel[QStringLiteral("defaultPosition")] = QJsonArray{QStringLiteral("left"), QStringLiteral("top")};
    contributes[QStringLiteral("flyViewPanel")] = panel;
    QVERIFY(PluginContributions::fromManifest(manifestWithContributes(contributes), &error).flyViewPanelItem.isEmpty());
    QVERIFY(!error.isEmpty());
}

void PluginContributionsTest::_unknownKeysIgnored_test()
{
    QJsonObject contributes = fullContributes();
    contributes[QStringLiteral("futureContribution")] = QJsonObject();

    QString error;
    const PluginContributions contributions =
        PluginContributions::fromManifest(manifestWithContributes(contributes), &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!contributions.toolMenuItem.isEmpty());
}

void PluginContributionsTest::_flags_test()
{
    QJsonObject contributes;
    contributes[QStringLiteral("replay")]           = true;
    contributes[QStringLiteral("telemetryLogging")] = false;

    QString error;
    const PluginContributions contributions =
        PluginContributions::fromManifest(manifestWithContributes(contributes), &error);
    QVERIFY(error.isEmpty());
    QVERIFY(contributions.providesReplayExtension);
    QVERIFY(!contributions.controlsTelemetryLogging);
    QVERIFY(contributions.toolMenuItem.isEmpty());
}

UT_REGISTER_TEST(PluginContributionsTest, TestLabel::Unit)
