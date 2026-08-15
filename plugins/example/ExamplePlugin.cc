#include "ExamplePlugin.h"

#include <QGCPluginAPI/QGCAppService.h>
#include <QGCPluginAPI/QGCHostServices.h>

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

    if (!_host) {
        return;
    }

    // A service is acquired by its versioned id and cast to the SDK interface.
    // An unknown id returns nullptr, so the cast result is always checked.
    _appService = qobject_cast<QGCAppService*>(_host->service(QGCAppServiceId));
    if (!_appService) {
        qCDebug(ExamplePluginLog) << "Host provides no" << QGCAppServiceId << "service";
        return;
    }

    _logSavePaths("at init");

    // Shape 1: the plugin itself is the connection's context, so the connection
    // dies with the plugin object — but not before, which is why cleanup()
    // disconnects it rather than leaving it to the destructor.
    connect(_appService, &QGCAppService::savePathsChanged, this, [this]() { _logSavePaths("changed"); });

    // Shape 2: a separate object owns the connection. It is outside this plugin's
    // QObject tree, so destroying the plugin does not destroy it; cleanup() must.
    _savePathWatcher = std::make_unique<QObject>();
    connect(_appService, &QGCAppService::savePathsChanged, _savePathWatcher.get(),
            []() { qCDebug(ExamplePluginLog) << "Save-path watcher notified"; });
}

void ExampleRuntimePlugin::cleanup()
{
    // init() run backwards. The host calls this on every deactivation — a user
    // disabling the plugin, a reload, or shutdown — and may call init() again
    // afterwards with the same host services object, so anything left connected
    // here is duplicated wiring on the next activation, not a one-off leak.
    _savePathWatcher.reset();

    if (_appService) {
        // The host counts connections *before* it destroys the plugin, so the
        // implicit disconnect at destruction is too late to satisfy the contract.
        disconnect(_appService, nullptr, this, nullptr);
        _appService = nullptr;
    }

    _host = nullptr;
    qCDebug(ExamplePluginLog) << "ExampleRuntimePlugin cleaned up";
}

void ExampleRuntimePlugin::_logSavePaths(const char* reason) const
{
    if (!_appService) {
        return;
    }
    qCDebug(ExamplePluginLog) << "Host save paths" << reason << "-" << _appService->savePath() << "/"
                              << _appService->telemetrySavePath();
}
