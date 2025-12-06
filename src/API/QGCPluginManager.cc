/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCPluginManager.h"
#include "QGCPlugin.h"
#include "QGCPluginLoader.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "PluginSettings.h"

#include <QtCore/QApplicationStatic>
#include <QtQml/qqml.h>

QGC_LOGGING_CATEGORY(QGCPluginManagerLog, "API.QGCPluginManager");

Q_APPLICATION_STATIC(QGCPluginManager, _qgcPluginManagerInstance);

QGCPluginManager::QGCPluginManager(QObject *parent)
    : QObject(parent)
{
    qCDebug(QGCPluginManagerLog) << this;
}

QGCPluginManager::~QGCPluginManager()
{
    qCDebug(QGCPluginManagerLog) << this;
    cleanup();
}

QGCPluginManager *QGCPluginManager::instance()
{
    return _qgcPluginManagerInstance();
}

void QGCPluginManager::registerQmlTypes()
{
    qmlRegisterUncreatableType<QGCPluginManager>("QGroundControl", 1, 0, "QGCPluginManager", "Reference only");
}

void QGCPluginManager::init()
{
    _loadPlugins();
}

void QGCPluginManager::cleanup()
{
    // Clean up plugins
    for (QGCPlugin* plugin : _loadedPlugins) {
        if (plugin) {
            plugin->cleanup();
            delete plugin;
        }
    }
    _loadedPlugins.clear();
    _toolMenuItems.clear();
}

QVariantList QGCPluginManager::loadedPlugins() const
{
    QVariantList pluginList;
    for (const QGCPlugin* plugin : _loadedPlugins) {
        if (plugin) {
            QVariantMap pluginInfo;
            pluginInfo["name"] = plugin->name();
            pluginList.append(pluginInfo);
        }
    }
    return pluginList;
}

void QGCPluginManager::addToolMenuItem(const QVariantMap& item)
{
    _toolMenuItems.append(item);
    emit toolMenuItemsChanged();
}

void QGCPluginManager::_loadPlugins()
{
    qCDebug(QGCPluginManagerLog) << "=== Plugin Loading Start ===";

    QGCPluginLoader loader(this);

    // Get default plugin search paths
    QStringList pluginPaths = QGCPluginLoader::defaultPluginPaths();
    
    qCDebug(QGCPluginManagerLog) << "Plugin search paths:" << pluginPaths;

    // Load plugins from all search paths
    loader.loadPlugins(pluginPaths);

    // Store loaded plugins
    _loadedPlugins = loader.loadedPlugins();

    qCDebug(QGCPluginManagerLog) << "Loaded" << _loadedPlugins.size() << "plugin(s)";

    // Get plugin settings to register plugins
    PluginSettings* pluginSettings = SettingsManager::instance()->pluginSettings();

    // Initialize all plugins and add their tool menu items
    for (QGCPlugin* plugin : _loadedPlugins) {
        qCDebug(QGCPluginManagerLog) << "Initializing plugin:" << plugin->name();
        
        // Register plugin with settings system using name as identifier
        pluginSettings->registerPlugin(plugin->name());
        
        // Always initialize the plugin
        plugin->init();
        
        // Get plugin's tool menu item and add it with visibility controlled by enabled Fact
        QVariantMap menuItem = plugin->toolMenuItem();
        if (!menuItem.isEmpty()) {
            qCDebug(QGCPluginManagerLog) << "  - Provides menu item:" << menuItem["title"];
            
            // Store the plugin name with the menu item so we can check enabled state dynamically
            menuItem["pluginName"] = plugin->name();
            
            addToolMenuItem(menuItem);
        } else {
            qCDebug(QGCPluginManagerLog) << "  - No menu item provided";
        }
    }

    qCDebug(QGCPluginManagerLog) << "=== Plugin Loading Complete:" << _loadedPlugins.size() << "plugin(s) active ===";
}
