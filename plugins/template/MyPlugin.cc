#include "MyPlugin.h"

#include <QtCore/QDebug>

// A MODULE plugin is loaded via dlopen/QPluginLoader, not linked normally, so the
// AUTORCC-generated resource-registration constructor can be stripped by the linker
// unless something references it explicitly — Q_INIT_RESOURCE is that reference.
// Without this, qrc:/qml/TemplatePanel.qml (declared in qgcplugin.json.in's
// flyViewPanel contribution) may fail to resolve at runtime. See plugins/example/
// ExamplePlugin.cc, which needs the identical call for the identical reason.
static void initializePluginResources()
{
    Q_INIT_RESOURCE(MyPlugin);
}

MyPlugin::MyPlugin(QObject *parent)
    : QObject(parent)
{
    initializePluginResources();
}

QGCPlugin *MyPlugin::createPlugin(QObject *parent)
{
    return new MyRuntimePlugin(parent);
}

MyRuntimePlugin::MyRuntimePlugin(QObject *parent)
    : QGCPlugin(parent)
{
}

void MyRuntimePlugin::init(QGCHostServices *host)
{
    _host = host;
    // Acquire a host service by its versioned id and cast to the matching SDK
    // interface, e.g.:
    //     auto* vehicles = qobject_cast<QGCVehicleService*>(host->service(QGCVehicleServiceId));
    qDebug() << "MyPlugin initialized" << (host ? "with host services" : "without host services");
}
