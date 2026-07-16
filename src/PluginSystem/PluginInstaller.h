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
#include <QtCore/QString>

Q_DECLARE_LOGGING_CATEGORY(PluginInstallerLog)

/// @brief Result of a PluginInstaller operation
struct PluginInstallResult {
    bool success = false;
    QString errorString;   ///< Reason for failure; empty on success
    QString pluginId;       ///< Manifest id of the installed/removed package
};

/// @brief Installs and removes .qgcplugin packages from the user plugins directory (D8, U3.2)
///
/// A .qgcplugin file is a zip archive with a package directory's contents at its root
/// (qgcplugin.json, optionally bin/<platform>/...). Extraction is done in-process via
/// vendored miniz (libs/miniz/ — see its README for why not QGCCompression/libarchive)
/// rather than shelling out to an external unzip tool: files QGC itself writes are not
/// quarantined by Gatekeeper, so the extracted package loads cleanly without a user
/// having to fight com.apple.quarantine on every file (01 §1.4).
class PluginInstaller
{
public:
    PluginInstaller() = delete;

    /// @brief Install a .qgcplugin package from a zip file into the user plugins directory
    /// Reads and validates qgcplugin.json at the archive root before extracting anything;
    /// a malformed or missing manifest is rejected without writing to disk. Extracts to
    /// <user-plugins-dir>/<manifest.id>/, replacing any existing install of the same id.
    /// @param zipPath Absolute path to the .qgcplugin file
    /// @return Result with the installed plugin's id on success
    static PluginInstallResult installFromFile(const QString& zipPath);

    /// @brief Remove an installed package by manifest id
    /// Deletes <user-plugins-dir>/<pluginId>/ entirely. Does not touch bundle-shipped
    /// plugins (only packages under the user plugins directory are removable this way);
    /// the caller is responsible for deactivating the plugin first.
    /// @param pluginId Manifest id of the package to remove
    static PluginInstallResult removePlugin(const QString& pluginId);

    /// @brief The directory packages are installed into (the user-writable entry of
    /// QGCPluginLoader::defaultPluginPaths())
    static QString userPluginsDir();

#if defined(Q_OS_MACOS)
    /// @brief True if packageDir's manifest file carries the com.apple.quarantine
    /// extended attribute (set by the OS when a file arrives via a quarantine-aware
    /// app, e.g. a browser download unzipped by Finder). A package extracted
    /// in-process by installFromFile() never carries it; this is for packages a
    /// user drops into the plugins directory by hand (01 §1.4). Checking the manifest
    /// alone (rather than every file) is a deliberate cost tradeoff: this runs on every
    /// discovered package at each app startup, and quarantine is applied uniformly by
    /// the OS to an entire extracted/copied tree from one archive-expand event, so one
    /// representative file is sufficient without a full recursive directory walk.
    /// @param packageDir Absolute path to a package directory
    static bool isQuarantined(const QString& packageDir);

    /// @brief Strip the com.apple.quarantine attribute from every file under packageDir
    /// Consent-gated: call only after explicit user approval to run a downloaded plugin.
    /// @param packageDir Absolute path to a package directory
    /// @return true if the attribute was removed (or was never present) on every file
    static bool stripQuarantine(const QString& packageDir);
#endif
};
