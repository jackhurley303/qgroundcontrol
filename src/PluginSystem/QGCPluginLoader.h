/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "PluginManifest.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QString>

class QGCPlugin;

Q_DECLARE_LOGGING_CATEGORY(QGCPluginLoaderLog)

/// @brief Lifecycle state of a discovered plugin
enum class PluginState {
    Discovered,     ///< Manifest read and validated; code has not run
    Incompatible,   ///< Manifest valid but rejected for this host (see errorString)
    Disabled,       ///< Valid but disabled by settings; never activated
    Active,         ///< Instantiated and running
    Failed,         ///< Metadata unreadable/invalid, or activation failed (see errorString)
    Quarantined,    ///< Skipped after a crash during a previous load attempt
};

/// @brief Information about a discovered plugin
/// The manifest and state are populated by inspection, before any plugin code runs.
struct PluginLoadInfo {
    QGCPlugin* plugin = nullptr;    ///< Plugin instance, non-null only when state == Active
    QString filePath;               ///< Absolute path to plugin file
    PluginManifest manifest;        ///< Declared identity/compatibility (valid unless state == Failed)
    PluginState state = PluginState::Failed;
    QString errorString;            ///< Reason for Incompatible/Failed states
};

/// @brief Discovers and loads QGC plugins at runtime
/// Scans specified directories for plugin libraries and loads them dynamically
class QGCPluginLoader : public QObject
{
    Q_OBJECT

public:
    explicit QGCPluginLoader(QObject* parent = nullptr);
    ~QGCPluginLoader();

    /// @brief Load all plugins from the specified directory
    /// @param pluginDir Absolute path to directory containing plugin libraries
    void loadPlugins(const QString& pluginDir);

    /// @brief Load all plugins from multiple directories
    /// @param pluginDirs List of absolute paths to directories containing plugins
    void loadPlugins(const QStringList& pluginDirs);

    /// @brief Load a single plugin from a specific file path
    /// @param filePath Absolute path to plugin library file
    /// @return PluginLoadInfo with plugin instance and path, or nullptr plugin on failure
    PluginLoadInfo loadPlugin(const QString& filePath);

    /// @brief Get list of successfully loaded plugins
    /// @return List of QGCPlugin instances
    QList<QGCPlugin*> loadedPlugins() const;

    /// @brief Get list of active plugins with their file paths
    /// @return List of PluginLoadInfo structs with state == Active
    QList<PluginLoadInfo> loadedPluginInfos() const;

    /// @brief Get all discovered plugins, including those that failed inspection or activation
    /// @return List of PluginLoadInfo structs in any state
    QList<PluginLoadInfo> knownPluginInfos() const { return _pluginInfos; }

    /// @brief Get default plugin search paths for the current platform
    /// @return List of directories where plugins should be searched
    static QStringList defaultPluginPaths();

    /// @brief Identity/compatibility facts of the running host, used to validate manifests
    static HostInfo hostInfo();

signals:
    /// @brief Emitted when a plugin is successfully loaded
    /// @param pluginName Name of the loaded plugin
    void pluginLoaded(const QString& pluginName);

    /// @brief Emitted when a plugin fails to load
    /// @param filePath Path to the plugin file that failed
    /// @param errorString Description of the error
    void pluginLoadFailed(const QString& filePath, const QString& errorString);

private:
    /// @brief Phase 1: read and validate the plugin's metadata without executing plugin code
    /// @param filePath Absolute path to plugin library file
    /// @return PluginLoadInfo in state Discovered, Incompatible, or Failed
    PluginLoadInfo _inspect(const QString& filePath);

    /// @brief Phase 2: instantiate a plugin that passed inspection
    /// @param info Inspection result in state Discovered; updated to Active or Failed
    void _activate(PluginLoadInfo& info);

    /// @brief Inspect, activate, record, and signal a single plugin file
    /// @param filePath Absolute path to plugin library file
    /// @return The recorded PluginLoadInfo
    PluginLoadInfo _loadPlugin(const QString& filePath);

    QList<PluginLoadInfo> _pluginInfos;  ///< All discovered plugins, any state
};
