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

void PluginSettings::registerPlugin(const QString& pluginName)
{
    qCDebug(PluginSettingsLog) << "Registering plugin:" << pluginName;
    if (_pluginFacts.contains(pluginName)) {
        return;  // Already registered
    }

    // Create metadata for the plugin enabled setting
    FactMetaData* metaData = new FactMetaData(FactMetaData::valueTypeBool, this);
    metaData->setName(pluginName);
    metaData->setLabel(pluginName);
    metaData->setShortDescription(QString("%1 Enabled").arg(pluginName));
    metaData->setLongDescription(QString("Enable or disable the %1 plugin. Changes take effect immediately.").arg(pluginName));

    // Example plugin is disabled by default, all others are enabled by default
    bool defaultEnabled = (pluginName != "Example");
    metaData->setRawDefaultValue(defaultEnabled);

    _nameToMetaDataMap[pluginName] = metaData;

    // Create the SettingsFact
    SettingsFact* fact = _createSettingsFact(pluginName);
    _pluginFacts[pluginName] = fact;

    emit registeredPluginsChanged();
}

Fact* PluginSettings::pluginEnabledFact(const QString& pluginName)
{
    if (_pluginFacts.contains(pluginName)) {
        return _pluginFacts[pluginName];
    }
    return nullptr;
}

bool PluginSettings::isPluginEnabled(const QString& pluginName)
{
    Fact* fact = pluginEnabledFact(pluginName);
    if (fact) {
        return fact->rawValue().toBool();
    }
    return true;  // Default to enabled if not found
}

QStringList PluginSettings::registeredPluginNames() const
{
    return _pluginFacts.keys();
}
