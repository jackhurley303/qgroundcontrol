#include "ExamplePlugin.h"

// Tier SDK plugins can't reach QGCLoggingCategory.h (host-internal, wired to
// QGCLoggingCategoryManager) — this is the plain-Qt equivalent of the host's
// QGC_LOGGING_CATEGORY macro, matching its default level (qCDebug silent
// unless enabled) without the host's runtime category registration/UI.
Q_LOGGING_CATEGORY(ExamplePluginLog, "PluginSystem.ExamplePlugin", QtWarningMsg)

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
}

void ExampleRuntimePlugin::init(QGCHostServices* host)
{
    _host = host;
    qCDebug(ExamplePluginLog) << "ExampleRuntimePlugin initialized" << (host ? "with host services" : "without host services");
}
