#include "ExamplePlugin.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(ExamplePluginLog, "PluginSystem.ExamplePlugin")

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
    qCDebug(ExamplePluginLog) << "ExamplePlugin factory created";
}

QGCPlugin* ExamplePlugin::createPlugin(QObject* parent)
{
    qCDebug(ExamplePluginLog) << "Creating ExamplePlugin instance";
    return new ExampleRuntimePlugin(parent);
}

// Runtime plugin implementation
ExampleRuntimePlugin::ExampleRuntimePlugin(QObject* parent)
    : QGCPlugin(parent)
{
    qCDebug(ExamplePluginLog) << "ExampleRuntimePlugin instance created";

    // Build tool menu item - enabled/disabled state is handled by PluginSettings
    _toolMenuItem["title"] = "Example";
    _toolMenuItem["icon"] = "/qmlimages/plugin.svg";
    _toolMenuItem["source"] = "qrc:/qml/ExamplePluginView.qml";
    _toolMenuItem["toolbarSource"] = "qrc:/qml/ExampleToolBar.qml";
}

QVariantMap ExampleRuntimePlugin::toolMenuItem() const
{
    return _toolMenuItem;
}
