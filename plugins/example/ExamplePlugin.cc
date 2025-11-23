#include "ExamplePlugin.h"

#include <QtCore/QDebug>

// Initialize plugin resources
void initializePluginResources() {
    Q_INIT_RESOURCE(ExamplePlugin);
}

// Plugin factory implementation
ExamplePlugin::ExamplePlugin(QObject* parent)
    : QObject(parent)
{
    // Initialize resources when plugin factory is created
    initializePluginResources();
    qDebug() << "ExamplePlugin factory created";
}

QGCCorePlugin* ExamplePlugin::createPlugin(QObject* parent)
{
    qDebug() << "ExamplePlugin: Creating plugin instance";
    return new ExampleCorePlugin(parent);
}

// Core plugin implementation
ExampleCorePlugin::ExampleCorePlugin(QObject* parent)
    : QGCCorePlugin(parent)
{
    qDebug() << "ExampleCorePlugin instance created";

    // Build tool menu items
    QVariantMap item;
    item["title"] = "Example Plugin";
    item["icon"] = "/res/QGCLogoFull.svg";
    item["source"] = "qrc:/qml/ExamplePluginView.qml";
    item["visible"] = true;

    _toolMenuItems.append(item);
}

const QVariantList& ExampleCorePlugin::toolMenuItems()
{
    qDebug() << "ExampleCorePlugin: Providing" << _toolMenuItems.size() << "tool menu items";
    return _toolMenuItems;
}
