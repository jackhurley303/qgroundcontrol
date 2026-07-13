#include "MyPlugin.h"

#include <QtCore/QDebug>

MyPlugin::MyPlugin(QObject *parent)
    : QObject(parent)
{
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
