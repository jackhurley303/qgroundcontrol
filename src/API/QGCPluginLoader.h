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
#include <QtCore/QList>
#include <QtCore/QString>

class QGCPlugin;

Q_DECLARE_LOGGING_CATEGORY(QGCPluginLoaderLog)

/// @brief Information about a loaded plugin
struct PluginLoadInfo {
    QGCPlugin* plugin;  ///< Plugin instance
    QString filePath;   ///< Absolute path to plugin file
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

    /// @brief Get list of loaded plugins with their file paths
    /// @return List of PluginLoadInfo structs
    QList<PluginLoadInfo> loadedPluginInfos() const { return _loadedPluginInfos; }

    /// @brief Get default plugin search paths for the current platform
    /// @return List of directories where plugins should be searched
    static QStringList defaultPluginPaths();

signals:
    /// @brief Emitted when a plugin is successfully loaded
    /// @param pluginName Name of the loaded plugin
    void pluginLoaded(const QString& pluginName);

    /// @brief Emitted when a plugin fails to load
    /// @param filePath Path to the plugin file that failed
    /// @param errorString Description of the error
    void pluginLoadFailed(const QString& filePath, const QString& errorString);

private:
    /// @brief Attempt to load a single plugin file
    /// @param filePath Absolute path to plugin library file
    /// @return PluginLoadInfo with plugin instance and path, or nullptr plugin on failure
    PluginLoadInfo _loadPlugin(const QString& filePath);

    /// @brief Validate plugin metadata
    /// @param plugin Plugin instance to validate
    /// @return true if plugin is valid and compatible
    bool _validatePlugin(QGCPlugin* plugin);

    QList<PluginLoadInfo> _loadedPluginInfos;  ///< List of loaded plugins with paths
};
