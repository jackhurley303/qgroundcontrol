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
    NeedsApproval,  ///< Discovered but requires explicit user consent before activation:
                     ///< every user-dir plugin without a recorded consent digest, and any
                     ///< plugin carrying com.apple.quarantine (D10/D15, U3.3)
};

/// @brief Information about a discovered plugin
/// The manifest and state are populated by inspection, before any plugin code runs.
struct PluginLoadInfo {
    QGCPlugin* plugin = nullptr;        ///< Plugin instance, non-null only when state == Active and tier != Qml
    QString filePath;                   ///< Absolute path to the plugin binary (bare dylib, or a package's resolved binary); the package directory itself for a tier-qml package
    QString packageDir;                 ///< Absolute path to the package directory (one containing qgcplugin.json at its root); empty for bare dev-loop dylibs
    PluginManifest manifest;            ///< Declared identity/compatibility (valid unless state == Failed)
    PluginContributions contributions;  ///< Declared contributions (valid unless state == Failed)
    PluginState state = PluginState::Failed;
    QString errorString;                ///< Reason for Incompatible/Failed states
    QString buildMarker;                ///< Per-build discriminator read from the mapped image after
                                         ///< activation (qgcPluginBuildMarker, via QLibrary::resolve);
                                         ///< empty until activated, or if the binary predates this symbol
    bool staleImage = false;            ///< The binary on disk changed after this process mapped it, so
                                         ///< the executing code is the older build (set by the manager
                                         ///< at activation; an in-place upgrade is the way in)
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

    /// @brief Read and validate a package's manifest without executing plugin code (D8)
    /// A package is a directory with qgcplugin.json at its root. Tier qml packages ship
    /// no binary; contributions are synthesized from the manifest alone, with relative
    /// URLs resolved package-relative. Other tiers require exactly one platform binary
    /// under bin/<platform>-*/ (documented key: bin/macos-universal/ on macOS).
    /// @param packageDir Absolute path to the package directory
    /// @return PluginLoadInfo in state Discovered, Incompatible, or Failed
    static PluginLoadInfo inspectPackage(const QString& packageDir);

    /// @brief Inspect every plugin library and package found in the given directories
    /// A child directory containing qgcplugin.json at its root is a package (D8);
    /// bare library files remain the dev-loop path. Both are supported side by side.
    /// @param pluginDirs List of absolute paths to directories containing plugins
    /// @return One PluginLoadInfo per discovered file/package; none are activated
    static QList<PluginLoadInfo> inspectDirectories(const QStringList& pluginDirs);

    /// @brief Instantiate a plugin that passed inspection
    /// Tier qml packages have no binary to instantiate: activation is trivial, leaving
    /// PluginLoadInfo::plugin null.
    /// @param info Inspection result in state Discovered; updated to Active or Failed
    static void activate(PluginLoadInfo& info);

    /// @brief Get default plugin search paths for the current platform
    /// @return List of directories where plugins should be searched
    static QStringList defaultPluginPaths();

    /// @brief Identity/compatibility facts of the running host, used to validate manifests
    static HostInfo hostInfo();
};
