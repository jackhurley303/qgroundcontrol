#pragma once

#include "API/QGCCorePlugin.h"
#include "API/QGCCorePluginInterface.h"

#include <QtCore/QObject>
#include <QtCore/QtPlugin>

/**
 * @class ExamplePlugin
 * @brief Example plugin demonstrating the QGC plugin architecture
 *
 * This is a minimal plugin that adds a custom tool menu item.
 */
class ExamplePlugin : public QObject, public QGCCorePluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.mavlink.qgroundcontrol.QGCCorePluginInterface")
    Q_INTERFACES(QGCCorePluginInterface)

public:
    explicit ExamplePlugin(QObject* parent = nullptr);
    ~ExamplePlugin() override = default;

    // QGCCorePluginInterface interface
    int pluginInterfaceVersion() const override { return 1; }
    QGCCorePlugin* createPlugin(QObject* parent) override;
};

/**
 * @class ExampleCorePlugin
 * @brief Core plugin implementation for the example plugin
 */
class ExampleCorePlugin : public QGCCorePlugin
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ExampleCorePlugin(QObject* parent = nullptr);
    ~ExampleCorePlugin() override = default;

    // Override to provide custom tool menu items
    const QVariantList& toolMenuItems() override;

private:
    QVariantList _toolMenuItems;
};
