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

#include <QtCore/QSettings>

DECLARE_SETTINGGROUP(Plugin, "Plugins")
{
    // Settings are dynamically created per plugin
}

void PluginSettings::registerPlugin(const QString& name)
{
    qDebug() << "Registering plugin:" << name;
    if (_pluginFacts.contains(name)) {
        return;  // Already registered
    }

    // Create metadata for the plugin enabled setting
    FactMetaData* metaData = new FactMetaData(FactMetaData::valueTypeBool, this);
    metaData->setName(name);
    metaData->setShortDescription(QString("%1 Enabled").arg(name));
    metaData->setLongDescription(QString("Enable or disable the %1 plugin. Changes take effect immediately.").arg(name));
    
    // Example plugin is disabled by default, all others are enabled by default
    bool defaultEnabled = (name != "Example");
    metaData->setRawDefaultValue(defaultEnabled);
    
    _nameToMetaDataMap[name] = metaData;

    // Create the SettingsFact
    SettingsFact* fact = _createSettingsFact(name);
    _pluginFacts[name] = fact;
}

Fact* PluginSettings::pluginEnabledFact(const QString& name)
{
    if (_pluginFacts.contains(name)) {
        return _pluginFacts[name];
    }
    return nullptr;
}

bool PluginSettings::isPluginEnabled(const QString& name)
{
    Fact* fact = pluginEnabledFact(name);
    if (fact) {
        return fact->rawValue().toBool();
    }
    return true;  // Default to enabled if not found
}
