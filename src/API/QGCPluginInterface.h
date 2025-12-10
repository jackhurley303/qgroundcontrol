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
/// @note Interface Version 1
/// When making breaking changes, increment the version number and update
/// all plugins accordingly. The loader validates version compatibility.
class QGCPluginInterface
{
public:
    virtual ~QGCPluginInterface() = default;

    /// @brief Returns the plugin interface version
    /// Must return 1 for this version of the interface
    /// @note If you change the interface, increment this version and update the IID
    virtual int pluginInterfaceVersion() const = 0;

    /// @brief Creates an instance of the plugin
    /// @param parent Parent QObject for the plugin
    /// @return A new QGCPlugin instance
    virtual QGCPlugin* createPlugin(QObject* parent) = 0;
};

#define QGCPluginInterface_iid "org.qgroundcontrol.QGCPluginInterface/1.0"

Q_DECLARE_INTERFACE(QGCPluginInterface, QGCPluginInterface_iid)
