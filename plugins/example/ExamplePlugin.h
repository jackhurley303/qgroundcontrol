#pragma once

#include "PluginAPI/QGCPlugin.h"
#include "PluginAPI/QGCPluginInterface.h"

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
    Q_PLUGIN_METADATA(IID QGCPluginInterface_iid FILE "qgcplugin.json")
    Q_INTERFACES(QGCPluginInterface)

public:
    explicit ExamplePlugin(QObject* parent = nullptr);
    ~ExamplePlugin() override = default;

    // QGCPluginInterface interface
    int pluginInterfaceVersion() const override { return QGCPluginApiVersion; }
    QGCPlugin* createPlugin(QObject* parent) override;
};

/**
 * @class ExampleRuntimePlugin
 * @brief Runtime plugin implementation for the example plugin.
 *
 * This serves as a template for developers building their own plugins.
 * Contributions (tool menu entry, fly/plan-view panels, replay and
 * telemetry-logging flags) are declared as data in qgcplugin.json.in —
 * see its "contributes" object. Code is only needed for behaviour:
 * override init(host)/cleanup() for lifecycle work and replayExtension()
 * to provide a flight replay extension.
 */
class ExampleRuntimePlugin : public QGCPlugin
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ExampleRuntimePlugin(QObject* parent = nullptr);
    ~ExampleRuntimePlugin() override = default;
};
