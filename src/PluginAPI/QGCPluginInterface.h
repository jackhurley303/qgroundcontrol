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
/// @ingroup PluginAPI
/// Plugins must implement this interface to be loadable by QGCPluginLoader
///
/// @warning This vtable is frozen. It crosses the SDK boundary, so adding,
/// removing, or reordering virtuals breaks every built plugin silently. There
/// is no in-place "breaking change": a new API major is a new IID and a new SDK
/// dylib soname, and all plugins rebuild against it in lockstep. Additions go to
/// a new interface, never here. See QGC_PLUGIN_API_VERSION_MAJOR below.
class QGCPluginInterface
{
public:
    virtual ~QGCPluginInterface() = default;

    /// @brief Returns the plugin interface version
    /// Must return QGCPluginApiVersion, the API major this plugin was compiled
    /// against. Belt-and-braces behind the manifest gate — keep it forever.
    virtual int pluginInterfaceVersion() const = 0;

    /// @brief Creates an instance of the plugin
    /// @param parent Parent QObject for the plugin
    /// @return A new QGCPlugin instance
    virtual QGCPlugin* createPlugin(QObject* parent) = 0;
};

/// Plugin API major version. The single owner of this fact: the interface IID and
/// the host's manifest 'apiVersion' gate both derive from it. Bump it (only) here
/// when the interface changes; all plugins rebuild in lockstep.
///
/// Cross-reference: the SDK dylib's VERSION/SOVERSION (src/PluginAPI/CMakeLists.txt)
/// tracks this — the rule is SOVERSION == API major, bumped together. A frozen
/// plugin's LC_LOAD_DYLIB records libQGCPluginAPI.<SOVERSION>.dylib, so a major
/// bump that forgets SOVERSION leaves old binaries resolving against a new-ABI dylib.
#define QGC_PLUGIN_API_VERSION_MAJOR 2

#define QGC_PLUGIN_API_STRINGIFY_2(x) #x
#define QGC_PLUGIN_API_STRINGIFY(x) QGC_PLUGIN_API_STRINGIFY_2(x)
#define QGCPluginInterface_iid "org.qgroundcontrol.QGCPluginAPI/" QGC_PLUGIN_API_STRINGIFY(QGC_PLUGIN_API_VERSION_MAJOR) ".0"

/// Plugin API major version supported by this host. A plugin manifest's 'apiVersion'
/// must match this value exactly.
inline constexpr int QGCPluginApiVersion = QGC_PLUGIN_API_VERSION_MAJOR;

Q_DECLARE_INTERFACE(QGCPluginInterface, QGCPluginInterface_iid)
