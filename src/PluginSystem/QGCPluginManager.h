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
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

#include "QGCReplayExtension.h"

class QGCPlugin;
struct PluginLoadInfo;

Q_DECLARE_LOGGING_CATEGORY(QGCPluginManagerLog)

/**
 * @class QGCPluginManager
 * @brief Manages runtime-loaded QGroundControl plugins
 *
 * This singleton class is responsible for discovering, loading, and managing
 * runtime plugins independently from the core QGC functionality. It provides
 * a clean separation between core plugin behavior (QGCCorePlugin) and runtime
 * plugin management.
 */
class QGCPluginManager : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("")
    Q_PROPERTY(QVariantList         loadedPlugins    READ loadedPlugins    NOTIFY loadedPluginsChanged)
    Q_PROPERTY(QVariantList         toolMenuItems    READ toolMenuItems    NOTIFY toolMenuItemsChanged)
    Q_PROPERTY(QGCReplayExtension*  replayExtension  READ replayExtension  NOTIFY replayExtensionChanged)
    Q_PROPERTY(QVariantList         flyViewPanelItems READ flyViewPanelItems NOTIFY flyViewPanelItemsChanged)

public:
    explicit QGCPluginManager(QObject *parent = nullptr);
    ~QGCPluginManager() override;

    static QGCPluginManager *instance();
    static void registerQmlTypes();

    /// Initialize the plugin manager and load plugins
    void init();

    /// Cleanup all loaded plugins
    void cleanup();

    /// Get the list of loaded plugins (for QML)
    /// @return A list of loaded plugin info as QVariantList
    QVariantList loadedPlugins() const;

    /// Get the list of tool menu items from all loaded plugins
    /// @return A list of tool menu items
    QVariantList toolMenuItems() const { return _toolMenuItems; }

    /// Get the replay extension provided by any loaded plugin, or nullptr if none.
    QGCReplayExtension* replayExtension() const { return _replayExtension; }

    /// Get the list of fly-view panel items from all loaded plugins.
    /// Each item is a QVariantMap with keys: name, panelUrl
    QVariantList flyViewPanelItems() const { return _flyViewPanelItems; }

    /// Add a tool menu item from a loaded plugin
    /// @param item QVariantMap with keys: title, icon, source, visible
    void addToolMenuItem(const QVariantMap& item);

    /// Unload a specific plugin by name
    /// @param pluginName The name of the plugin to unload
    Q_INVOKABLE void unloadPlugin(const QString& pluginName);

    /// Reload a specific plugin by name
    /// @param pluginName The name of the plugin to reload
    Q_INVOKABLE void reloadPlugin(const QString& pluginName);

signals:
    /// Emitted when the tool menu items list changes
    void toolMenuItemsChanged();

    /// Emitted when the loaded plugins list changes
    void loadedPluginsChanged();

    /// Emitted when the replay extension changes (plugin loaded or unloaded)
    void replayExtensionChanged();

    /// Emitted when the fly-view panel items list changes
    void flyViewPanelItemsChanged();

private:
    void _loadPlugins();
    void _removeToolMenuItemsForPlugin(const QString& pluginName);
    void _addLoadedPlugin(const PluginLoadInfo& loadInfo);

    struct PluginInfo {
        QGCPlugin* plugin;
        QString path;
        QString name;
    };

    QVariantList _toolMenuItems;           // List of tool menu items (from plugins)
    QVariantList _flyViewPanelItems;       // List of fly-view panel items (from plugins)
    QList<PluginInfo> _loadedPluginInfos; // List of loaded plugins with their info
    QGCReplayExtension* _replayExtension = nullptr; // First replay extension found across loaded plugins
};
