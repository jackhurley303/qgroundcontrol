/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginSettings.h"
#include "FactMetaData.h"
#include "SettingsFact.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QSettings>

QGC_LOGGING_CATEGORY(PluginSettingsLog, "PluginSystem.PluginSettings")

DECLARE_SETTINGGROUP(Plugin, "Plugins")
{
    // Settings are dynamically created per plugin
}

void PluginSettings::registerPlugin(const QString& pluginId, const QString& displayName, bool defaultEnabled)
{
    qCDebug(PluginSettingsLog) << "Registering plugin:" << pluginId;
    if (_pluginFacts.contains(pluginId)) {
        return;  // Already registered
    }

    // Create metadata for the plugin enabled setting
    FactMetaData* metaData = new FactMetaData(FactMetaData::valueTypeBool, this);
    metaData->setName(pluginId);
    metaData->setLabel(displayName);
    metaData->setShortDescription(QString("%1 Enabled").arg(displayName));
    metaData->setLongDescription(QString("Enable or disable the %1 plugin. Changes take effect immediately.").arg(displayName));
    metaData->setRawDefaultValue(defaultEnabled);

    _nameToMetaDataMap[pluginId] = metaData;

    // Create the SettingsFact
    SettingsFact* fact = _createSettingsFact(pluginId);
    _pluginFacts[pluginId] = fact;

    emit registeredPluginsChanged();
}

Fact* PluginSettings::pluginEnabledFact(const QString& pluginId)
{
    if (_pluginFacts.contains(pluginId)) {
        return _pluginFacts[pluginId];
    }
    return nullptr;
}

bool PluginSettings::isPluginEnabled(const QString& pluginId)
{
    Fact* fact = pluginEnabledFact(pluginId);
    if (fact) {
        return fact->rawValue().toBool();
    }
    return true;  // Default to enabled if not found
}

QStringList PluginSettings::registeredPluginIds() const
{
    return _pluginFacts.keys();
}
