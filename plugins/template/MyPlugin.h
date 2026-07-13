#pragma once

#include <QGCPluginAPI/QGCPlugin.h>
#include <QGCPluginAPI/QGCPluginInterface.h>

#include <QtCore/QObject>
#include <QtCore/QtPlugin>

class QGCHostServices;

/// Plugin factory. QGCPluginLoader instantiates this via QPluginLoader only after
/// validating qgcplugin.json (PluginManifest) without running any plugin code —
/// createPlugin() is the first line of this plugin's code the host ever runs.
class MyPlugin : public QObject, public QGCPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGCPluginInterface_iid FILE "qgcplugin.json")
    Q_INTERFACES(QGCPluginInterface)

public:
    explicit MyPlugin(QObject *parent = nullptr);

    int pluginInterfaceVersion() const override { return QGCPluginApiVersion; }
    QGCPlugin *createPlugin(QObject *parent) override;
};

/// Runtime instance. Add contributions (tool menu entry, fly/plan-view panels, replay
/// and telemetry-logging flags) as data in qgcplugin.json.in's "contributes" object —
/// see SDK-README.md's manifest schema. Code here is only for behavior: override
/// init(host)/cleanup() for lifecycle work and replayExtension() for a replay hook.
class MyRuntimePlugin : public QGCPlugin
{
    Q_OBJECT

public:
    explicit MyRuntimePlugin(QObject *parent = nullptr);

    void init(QGCHostServices *host) override;

private:
    QGCHostServices *_host = nullptr;
};
