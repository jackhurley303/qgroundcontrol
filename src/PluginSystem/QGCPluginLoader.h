/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "PluginContributions.h"
#include "PluginManifest.h"

#include <QtCore/QLoggingCategory>
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
    QGCPlugin* plugin = nullptr;        ///< Plugin instance, non-null only when state == Active
    QString filePath;                   ///< Absolute path to plugin file
    PluginManifest manifest;            ///< Declared identity/compatibility (valid unless state == Failed)
    PluginContributions contributions;  ///< Declared contributions (valid unless state == Failed)
    PluginState state = PluginState::Failed;
    QString errorString;                ///< Reason for Incompatible/Failed states
};

/// @brief Stateless inspect/activate mechanism for QGC plugin libraries
/// Inspection reads and validates metadata without executing plugin code; activation
/// instantiates a plugin that passed inspection. Policy (enabled state, consent) and
/// record keeping live in QGCPluginManager.
class QGCPluginLoader
{
public:
    QGCPluginLoader() = delete;

    /// @brief Read and validate a plugin's metadata without executing plugin code
    /// @param filePath Absolute path to plugin library file
    /// @return PluginLoadInfo in state Discovered, Incompatible, or Failed
    static PluginLoadInfo inspect(const QString& filePath);

    /// @brief Inspect every plugin library found in the given directories
    /// @param pluginDirs List of absolute paths to directories containing plugins
    /// @return One PluginLoadInfo per discovered file; none are activated
    static QList<PluginLoadInfo> inspectDirectories(const QStringList& pluginDirs);

    /// @brief Instantiate a plugin that passed inspection
    /// @param info Inspection result in state Discovered; updated to Active or Failed
    static void activate(PluginLoadInfo& info);

    /// @brief Get default plugin search paths for the current platform
    /// @return List of directories where plugins should be searched
    static QStringList defaultPluginPaths();

    /// @brief Identity/compatibility facts of the running host, used to validate manifests
    static HostInfo hostInfo();
};
