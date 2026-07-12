#pragma once

#include <QGCPluginAPI/QGCPlugin.h>
#include <QGCPluginAPI/QGCPluginInterface.h>

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QtPlugin>

class QGCHostServices;

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

public:
    explicit ExampleRuntimePlugin(QObject* parent = nullptr);
    ~ExampleRuntimePlugin() override = default;

    // This plugin needs no host services yet; init() just stores the pointer.
    // A real plugin would acquire one by id and qobject_cast, e.g.:
    //     auto* vehicles = qobject_cast<QGCVehicleService*>(host->service(QGCVehicleServiceId));
    void init(QGCHostServices* host) override;

private:
    QGCHostServices* _host = nullptr;
};
