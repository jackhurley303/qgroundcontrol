#pragma once

#include <QGCPluginAPI/QGCPlugin.h>
#include <QGCPluginAPI/QGCPluginInterface.h>

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QtPlugin>
#include <memory>

class QGCAppService;
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
 *
 * init()/cleanup() below are the reference implementation of the lifecycle
 * contract documented on QGCPlugin::cleanup(): the user can disable and
 * re-enable a plugin without restarting QGroundControl, so everything init()
 * builds, cleanup() must take back down. Both shapes a plugin actually uses
 * appear here — a connection whose context is the plugin itself, and one whose
 * context is a separate, unparented object — because only the second kind is
 * easy to forget.
 */
class ExampleRuntimePlugin : public QGCPlugin
{
    Q_OBJECT

public:
    explicit ExampleRuntimePlugin(QObject* parent = nullptr);
    ~ExampleRuntimePlugin() override = default;

    void init(QGCHostServices* host) override;
    void cleanup() override;

private:
    void _logSavePaths(const char* reason) const;

    QGCHostServices* _host = nullptr;

    /// Raw because the host owns it: the services object and everything registered
    /// on it outlive every plugin, and cleanup() runs before the host tears them
    /// down. A plugin holding a host pointer past cleanup() is the bug, not a
    /// dangling one.
    QGCAppService* _appService = nullptr;

    /// Deliberately NOT parented to this plugin: a plugin's helper objects
    /// routinely live outside its object tree, and those are exactly the ones a
    /// teardown misses — the host's connection census counts a connection this
    /// object still holds just as it counts one the plugin holds directly.
    /// cleanup() destroys it explicitly; the unique_ptr is what keeps that true if
    /// the plugin is ever destroyed without a cleanup() first.
    std::unique_ptr<QObject> _savePathWatcher;
};
