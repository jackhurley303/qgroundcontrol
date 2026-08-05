/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

#include "PluginRecordStore.h"
#include "QGCPluginLoader.h"
#include "QGCReplayExtension.h"

Q_DECLARE_LOGGING_CATEGORY(QGCPluginManagerLog)

class QGCHostServicesImpl;
class QQmlEngine;

/**
 * @class QGCPluginManager
 * @brief Manages runtime-loaded QGroundControl plugins
 *
 * This singleton owns one record per discovered plugin — in any state — and the
 * policy decisions around them: which plugins are enabled (settings keyed by
 * manifest id), which get activated, and what contributions they expose to QML.
 * Disabled and incompatible plugins are recorded from their manifest alone;
 * their code never executes.
 */
class QGCPluginManager : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("")
    Q_PROPERTY(QVariantList         loadedPlugins    READ loadedPlugins    NOTIFY loadedPluginsChanged)
    Q_PROPERTY(QVariantList         knownPlugins     READ knownPlugins     NOTIFY loadedPluginsChanged)
    Q_PROPERTY(QVariantList         toolMenuItems    READ toolMenuItems    NOTIFY toolMenuItemsChanged)
    Q_PROPERTY(QGCReplayExtension*  replayExtension  READ replayExtension  NOTIFY replayExtensionChanged)
    Q_PROPERTY(QVariantList         flyViewPanelItems  READ flyViewPanelItems  NOTIFY flyViewPanelItemsChanged)
    Q_PROPERTY(QVariantList         planViewPanelItems READ planViewPanelItems NOTIFY planViewPanelItemsChanged)

public:
    explicit QGCPluginManager(QObject *parent = nullptr);
    ~QGCPluginManager() override;

    static QGCPluginManager *instance();
    static void registerQmlTypes();

    /// Initialize the plugin manager and load plugins
    void init();

    /// Hand over the application's QML engine, once it exists, or nullptr when it
    /// goes away. A plugin activated after the engine has started resolving QML
    /// registers its resources too late for the engine's cached directory listings;
    /// the manager invalidates them at activation. Plugins loaded before the engine
    /// is built need nothing, so this may legitimately never be called (unit tests,
    /// headless runs).
    void setQmlEngine(QQmlEngine *engine);

    /// Cleanup all loaded plugins
    void cleanup();

    /// Get the list of loaded plugins (for QML)
    /// @return A list of loaded plugin info as QVariantList
    QVariantList loadedPlugins() const;

    /// Get every discovered plugin, in any state, for QML display (e.g. the Plugins
    /// settings page). Each item is a QVariantMap with keys: id, name, version, vendor,
    /// description, state (raw PluginState name, for UI color-coding), statusText
    /// (human-readable status line).
    /// @return A list of known plugin info as QVariantList
    QVariantList knownPlugins() const;

    /// Get the list of tool menu items from all loaded plugins.
    /// Each item is a QVariantMap with keys: pluginId, title, icon, source, toolbarSource
    QVariantList toolMenuItems() const { return _toolMenuItems; }

    /// Get the replay extension provided by any loaded plugin, or nullptr if none.
    QGCReplayExtension* replayExtension() const { return _replayExtension; }

    /// Get the list of fly-view panel items from all loaded plugins.
    /// Each item is a QVariantMap with keys: pluginId, name, panelUrl, dockUrl,
    /// defaultWidth, defaultHeight, defaultXFraction, defaultYFraction
    QVariantList flyViewPanelItems() const { return _flyViewPanelItems; }

    /// Get the list of plan-view panel items from all loaded plugins.
    /// Each item is a QVariantMap with the same keys as flyViewPanelItems()
    QVariantList planViewPanelItems() const { return _planViewPanelItems; }

    /// Returns true if any active plugin has claimed exclusive control of
    /// telemetry logging by declaring "telemetryLogging" in its manifest.
    bool hasLoggingController() const { return _hasLoggingController; }

    /// Enable or disable a plugin: persists the setting and activates or
    /// deactivates the plugin to match. Idempotent.
    /// @param pluginId The manifest id of the plugin
    Q_INVOKABLE void setPluginEnabled(const QString& pluginId, bool enabled);

    /// Reload a plugin from its stored path: deactivate if active, re-inspect
    /// the file, and activate again if the plugin is enabled.
    /// @param pluginId The manifest id of the plugin
    Q_INVOKABLE void reloadPlugin(const QString& pluginId);

    /// Install a .qgcplugin package from a zip file and add it as a new record
    /// (PluginInstaller::installFromFile, U3.2). Replaces any existing record with
    /// the same id (deactivating it first). The file-dialog pick counts as the D10
    /// consent, so the fresh record is approved and activated if enabled.
    /// @param zipPath Absolute path to the .qgcplugin file
    /// @return Empty string on success, otherwise a human-readable error
    Q_INVOKABLE QString installPlugin(const QString& zipPath);

    /// Remove an installed package: deactivate it, delete its directory
    /// (PluginInstaller::removePlugin), and drop its record.
    /// @param pluginId The manifest id of the package to remove
    /// @return Empty string on success, otherwise a human-readable error
    Q_INVOKABLE QString removePlugin(const QString& pluginId);

    /// Approve a plugin currently in the NeedsApproval state — the one consent flow
    /// (D10): strips the com.apple.quarantine attribute (macOS), records the consent
    /// digest so approval persists across restarts until the plugin's content changes,
    /// and activates the plugin if enabled.
    /// @param pluginId The manifest id of the plugin
    Q_INVOKABLE void approvePlugin(const QString& pluginId);

signals:
    /// Emitted when the tool menu items list changes
    void toolMenuItemsChanged();

    /// Emitted when the loaded plugins list changes
    void loadedPluginsChanged();

    /// Emitted when the replay extension changes (plugin loaded or unloaded)
    void replayExtensionChanged();

    /// Emitted when the fly-view panel items list changes
    void flyViewPanelItemsChanged();

    /// Emitted when the plan-view panel items list changes
    void planViewPanelItemsChanged();

private:
    void _loadPlugins();
    void _ensureHostServices();
    void _processInspected(const QList<PluginLoadInfo>& infos);
    void _activateRecord(PluginLoadInfo& record);
    void _deactivateRecord(PluginLoadInfo& record);
    void _addContributions(const PluginLoadInfo& record);
    void _removeContributionsForPlugin(const QString& pluginId);
    void _recalcReplayExtension();
    void _recalcLoggingController();
    /// The one mutation epilogue: both recalcs plus the emit, called at every
    /// site that changes _records' active membership.
    void _notifyRecordsChanged();
    void _activateIfEnabled(PluginLoadInfo& record);
    void _invalidateQmlCache(const PluginLoadInfo& record);
    QString _statusText(const PluginLoadInfo& record) const;

    QVariantList _toolMenuItems;           // List of tool menu items (from plugins)
    QVariantList _flyViewPanelItems;       // List of fly-view panel items (from plugins)
    QVariantList _planViewPanelItems;      // List of plan-view panel items (from plugins)
    PluginRecordStore _recordStore;        // Owns the record collection and all plugin persistence
    QGCHostServicesImpl* _hostServices = nullptr; // Service registry handed to every plugin's init()
    QGCReplayExtension* _replayExtension = nullptr; // First replay extension found across active plugins
    QPointer<QQmlEngine> _qmlEngine;      // Application QML engine, null until it exists (see setQmlEngine)
    bool _hasLoggingController = false;   // True if any active plugin claims telemetry-logging control

    friend class QGCPluginManagerTest;
};
