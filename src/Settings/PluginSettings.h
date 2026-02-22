/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtQmlIntegration/QtQmlIntegration>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMap>

#include "SettingsGroup.h"

Q_DECLARE_LOGGING_CATEGORY(PluginSettingsLog)

class QGCPlugin;

/// Plugin Settings - Manages enabled/disabled state for plugins
class PluginSettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    Q_PROPERTY(QStringList registeredPluginNames READ registeredPluginNames NOTIFY registeredPluginsChanged)

public:
    PluginSettings(QObject* parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    /// Register a plugin and create its enabled Fact
    Q_INVOKABLE void registerPlugin(const QString& pluginName);

    /// Get the enabled Fact for a plugin
    Q_INVOKABLE Fact* pluginEnabledFact(const QString& pluginName);

    /// Check if a plugin is enabled
    Q_INVOKABLE bool isPluginEnabled(const QString& pluginName);

    /// Get list of all registered plugin names
    QStringList registeredPluginNames() const;

signals:
    void registeredPluginsChanged();

private:
    QMap<QString, SettingsFact*> _pluginFacts;  ///< Map of plugin name to enabled Fact
};
