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

class QGCCorePlugin;

/// @brief Interface for QGC plugins
/// Plugins must implement this interface to be loadable by QGCPluginLoader
class QGCCorePluginInterface
{
public:
    virtual ~QGCCorePluginInterface() = default;

    /// @brief Returns the plugin interface version
    /// Must return 1 for this version of the interface
    virtual int pluginInterfaceVersion() const = 0;

    /// @brief Creates an instance of the plugin
    /// @param parent Parent QObject for the plugin
    /// @return A new QGCCorePlugin instance
    virtual QGCCorePlugin* createPlugin(QObject* parent) = 0;
};

#define QGCCorePluginInterface_iid "org.qgroundcontrol.QGCCorePluginInterface/1.0"

Q_DECLARE_INTERFACE(QGCCorePluginInterface, QGCCorePluginInterface_iid)
