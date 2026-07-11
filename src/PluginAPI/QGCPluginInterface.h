/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QtPlugin>

class QGCPlugin;

/// @brief Interface for QGC plugins
/// Plugins must implement this interface to be loadable by QGCPluginLoader
///
/// @note Interface Version 2
/// When making breaking changes, increment the version number and update
/// all plugins accordingly. The loader validates version compatibility.
class QGCPluginInterface
{
public:
    virtual ~QGCPluginInterface() = default;

    /// @brief Returns the plugin interface version
    /// Must return the QGCPluginApiVersion this plugin was compiled against
    /// @note If you change the interface, increment this version and update the IID
    virtual int pluginInterfaceVersion() const = 0;

    /// @brief Creates an instance of the plugin
    /// @param parent Parent QObject for the plugin
    /// @return A new QGCPlugin instance
    virtual QGCPlugin* createPlugin(QObject* parent) = 0;
};

/// Plugin API major version. The single owner of this fact: the interface IID and
/// the host's manifest 'apiVersion' gate both derive from it. Bump it (only) here
/// when the interface changes; all plugins rebuild in lockstep.
#define QGC_PLUGIN_API_VERSION_MAJOR 2

#define QGC_PLUGIN_API_STRINGIFY_2(x) #x
#define QGC_PLUGIN_API_STRINGIFY(x) QGC_PLUGIN_API_STRINGIFY_2(x)
#define QGCPluginInterface_iid "org.qgroundcontrol.QGCPluginAPI/" QGC_PLUGIN_API_STRINGIFY(QGC_PLUGIN_API_VERSION_MAJOR) ".0"

/// Plugin API major version supported by this host. A plugin manifest's 'apiVersion'
/// must match this value exactly.
inline constexpr int QGCPluginApiVersion = QGC_PLUGIN_API_VERSION_MAJOR;

Q_DECLARE_INTERFACE(QGCPluginInterface, QGCPluginInterface_iid)
