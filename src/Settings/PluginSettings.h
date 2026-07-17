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

/// Plugin Settings - Manages enabled/disabled state for plugins, keyed by manifest id
class PluginSettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    Q_PROPERTY(QStringList registeredPluginIds READ registeredPluginIds NOTIFY registeredPluginsChanged)

public:
    PluginSettings(QObject* parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    /// Register a plugin and create its enabled Fact
    /// @param pluginId Manifest id (reverse-DNS), used as the Fact key
    /// @param displayName Human-readable name shown in UI
    /// @param defaultEnabled Whether the plugin is enabled before the user ever toggles it
    void registerPlugin(const QString& pluginId, const QString& displayName, bool defaultEnabled);

    /// Get the enabled Fact for a plugin
    Q_INVOKABLE Fact* pluginEnabledFact(const QString& pluginId);

    /// Check if a plugin is enabled
    Q_INVOKABLE bool isPluginEnabled(const QString& pluginId);

    /// Get list of all registered plugin ids
    QStringList registeredPluginIds() const;

    /// Get the consent digest recorded when the user approved this plugin (D10).
    /// Empty if the plugin was never approved.
    /// @param pluginId Manifest id (reverse-DNS)
    QString approvedPluginDigest(const QString& pluginId) const;

    /// Record the consent digest for an approved plugin (D10). Not a Fact: consent
    /// is internal trust state keyed to the plugin's content, not a user-editable
    /// setting. A digest mismatch on a later scan re-prompts for approval.
    /// @param pluginId Manifest id (reverse-DNS)
    /// @param digest Digest as computed by QGCPluginManager (version + content hash)
    void setApprovedPluginDigest(const QString& pluginId, const QString& digest);

signals:
    void registeredPluginsChanged();

private:
    QMap<QString, SettingsFact*> _pluginFacts;  ///< Map of plugin id to enabled Fact
};
