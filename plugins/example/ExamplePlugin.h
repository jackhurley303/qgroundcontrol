#pragma once

#include "PluginSystem/QGCPlugin.h"
#include "PluginSystem/QGCPluginInterface.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QtPlugin>

Q_DECLARE_LOGGING_CATEGORY(ExamplePluginLog)

/**
 * @class ExamplePlugin
 * @brief Example plugin demonstrating the QGC plugin architecture
 *
 * This is a minimal plugin that adds a custom tool menu item.
 */
class ExamplePlugin : public QObject, public QGCPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.mavlink.qgroundcontrol.QGCPluginInterface")
    Q_INTERFACES(QGCPluginInterface)

public:
    explicit ExamplePlugin(QObject* parent = nullptr);
    ~ExamplePlugin() override = default;

    // QGCPluginInterface interface
    int pluginInterfaceVersion() const override { return 1; }
    QGCPlugin* createPlugin(QObject* parent) override;
};

/**
 * @class ExampleRuntimePlugin
 * @brief Runtime plugin implementation for the example plugin.
 *
 * This serves as a template for developers building their own plugins.
 * Each override below demonstrates one feature of the plugin system.
 */
class ExampleRuntimePlugin : public QGCPlugin
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ExampleRuntimePlugin(QObject* parent = nullptr);
    ~ExampleRuntimePlugin() override = default;

    // --- Required ---

    /// Unique display name for this plugin.
    QString name() const override { return QStringLiteral("Example"); }

    // --- Tool menu (optional) ---

    /// Adds an entry to the main tool menu that opens a full-screen view.
    /// Remove this method (or return an empty QVariantMap) if you don't need a tool menu entry.
    QVariantMap toolMenuItem() const override;

    // --- Fly-view panel (optional) ---

    /// QML component loaded inside the floating fly-view panel.
    /// Remove this method (or return an empty string) to opt out of a fly-view panel entirely.
    QString flyViewPanelUrl() const override { return QStringLiteral("qrc:/qml/ExampleFlyViewPanel.qml"); }

    /// QML component shown as the collapsed row in the fly-view dock strip.
    /// Remove this method (or return an empty string) to use the default plain-name label.
    QString flyViewPanelDockUrl() const override { return QStringLiteral("qrc:/qml/ExampleFlyViewDockItem.qml"); }

    /// Default panel width in units of ScreenTools.defaultFontPixelWidth.
    /// Return 0 to use the framework default (30 units).
    double flyViewPanelDefaultWidth() const override { return 35; }

    /// Default panel height in units of ScreenTools.defaultFontPixelHeight.
    /// Return 0 to use the framework default (15 units).
    double flyViewPanelDefaultHeight() const override { return 18; }

    /// Default panel position as fractions [0, 1] of the parent size.
    /// (0, 0) = top-left, (1, 0) = top-right, (0, 1) = bottom-left, (1, 1) = bottom-right.
    /// Return (-1, -1) to use the framework default (staggered from the right edge).
    QPointF flyViewPanelDefaultPosition() const override { return QPointF(0.0, 0.0); }

    // --- Plan-view panel (optional) ---

    /// QML component loaded inside the floating plan-view panel.
    /// Remove this method (or return an empty string) to opt out of a plan-view panel entirely.
    QString planViewPanelUrl() const override { return QStringLiteral("qrc:/qml/ExamplePlanViewPanel.qml"); }

    /// Default panel width in units of ScreenTools.defaultFontPixelWidth.
    /// Return 0 to use the framework default (30 units).
    double planViewPanelDefaultWidth() const override { return 35; }

    /// Default panel height in units of ScreenTools.defaultFontPixelHeight.
    /// Return 0 to use the framework default (15 units).
    double planViewPanelDefaultHeight() const override { return 14; }

    /// QML component shown as the collapsed row in the plan-view dock strip.
    /// Remove this method (or return an empty string) to use the default plain-name label.
    QString planViewPanelDockUrl() const override { return QStringLiteral("qrc:/qml/ExamplePlanViewDockItem.qml"); }

    /// Default panel position as fractions [0, 1] of the parent size.
    /// Return (-1, -1) to use the framework default (staggered from the right edge).
    QPointF planViewPanelDefaultPosition() const override { return QPointF(0.0, 0.0); }

private:
    QVariantMap _toolMenuItem;
};
