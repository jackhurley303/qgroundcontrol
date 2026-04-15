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

QGC_LOGGING_CATEGORY(QGCPluginManagerLog, "PluginSystem.QGCPluginManager");

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
    for (const PluginInfo& info : _loadedPluginInfos) {
        if (info.plugin) {
            info.plugin->cleanup();
            delete info.plugin;
        }
    }
    _loadedPluginInfos.clear();
    _toolMenuItems.clear();
    if (_replayExtension) {
        _replayExtension = nullptr;
        emit replayExtensionChanged();
    }
    _flyViewPanelItems.clear();
    emit flyViewPanelItemsChanged();
    emit loadedPluginsChanged();
    emit toolMenuItemsChanged();
}

QVariantList QGCPluginManager::loadedPlugins() const
{
    QVariantList pluginList;
    for (const PluginInfo& info : _loadedPluginInfos) {
        if (info.plugin) {
            QVariantMap pluginInfo;
            pluginInfo["name"] = info.name;
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

    // Store loaded plugins with their info
    QList<PluginLoadInfo> pluginInfos = loader.loadedPluginInfos();
    
    qCDebug(QGCPluginManagerLog) << "Loaded" << pluginInfos.size() << "plugin(s)";

    // Get plugin settings to register plugins
    PluginSettings* pluginSettings = SettingsManager::instance()->pluginSettings();

    // Initialize all plugins and add their tool menu items
    for (const PluginLoadInfo& loadInfo : pluginInfos) {
        QGCPlugin* plugin = loadInfo.plugin;
        QString pluginName = plugin->name();
        qCDebug(QGCPluginManagerLog) << "Processing plugin:" << pluginName << "from" << loadInfo.filePath;
        
        // Register plugin with settings system using name as identifier
        pluginSettings->registerPlugin(pluginName);
        
        // Check if plugin is enabled
        bool isEnabled = pluginSettings->isPluginEnabled(pluginName);
        qCDebug(QGCPluginManagerLog) << "  - Enabled:" << isEnabled;
        
        if (isEnabled) {
            // Create plugin info structure
            PluginInfo info;
            info.plugin = plugin;
            info.name = pluginName;
            info.path = loadInfo.filePath;  // Store the actual file path
            
            _loadedPluginInfos.append(info);
            
            // Initialize the plugin
            plugin->init();

            // Register replay extension if this plugin provides one and none is set yet
            if (!_replayExtension) {
                QGCReplayExtension* ext = plugin->replayExtension();
                if (ext) {
                    _replayExtension = ext;
                    emit replayExtensionChanged();
                }
            }

            // Get plugin's tool menu item and add it
            QVariantMap menuItem = plugin->toolMenuItem();
            if (!menuItem.isEmpty()) {
                qCDebug(QGCPluginManagerLog) << "  - Provides menu item:" << menuItem["title"];

                // Store the plugin name with the menu item so we can check enabled state dynamically
                menuItem["pluginName"] = pluginName;

                addToolMenuItem(menuItem);
            } else {
                qCDebug(QGCPluginManagerLog) << "  - No menu item provided";
            }

            // Register fly-view panel item if this plugin provides one
            QString panelUrl = plugin->flyViewPanelUrl();
            if (!panelUrl.isEmpty()) {
                QPointF defaultPos = plugin->flyViewPanelDefaultPosition();
                QVariantMap panelItem;
                panelItem["name"]           = pluginName;
                panelItem["panelUrl"]       = panelUrl;
                panelItem["dockUrl"]        = plugin->flyViewPanelDockUrl();
                panelItem["defaultWidth"]   = plugin->flyViewPanelDefaultWidth();
                panelItem["defaultHeight"]  = plugin->flyViewPanelDefaultHeight();
                panelItem["defaultXFraction"] = defaultPos.x();
                panelItem["defaultYFraction"] = defaultPos.y();
                _flyViewPanelItems.append(panelItem);
                emit flyViewPanelItemsChanged();
                qCDebug(QGCPluginManagerLog) << "  - Provides fly-view panel:" << panelUrl;
            }
        } else {
            // Plugin is disabled, don't load it
            qCDebug(QGCPluginManagerLog) << "  - Skipping disabled plugin";
            delete plugin; // Clean up since we're not loading it
        }
    }

    emit loadedPluginsChanged();
    qCDebug(QGCPluginManagerLog) << "=== Plugin Loading Complete:" << _loadedPluginInfos.size() << "plugin(s) active ===";
    
    // Refresh logging category settings now that plugin categories are registered
    QGCLoggingCategoryManager::instance()->setFilterRulesFromSettings(QString());
}

void QGCPluginManager::_removeToolMenuItemsForPlugin(const QString& pluginName)
{
    // Remove all tool menu items for this plugin
    for (int i = _toolMenuItems.size() - 1; i >= 0; --i) {
        QVariantMap item = _toolMenuItems[i].toMap();
        if (item["pluginName"].toString() == pluginName) {
            _toolMenuItems.removeAt(i);
        }
    }
    emit toolMenuItemsChanged();

    // Remove fly-view panel item for this plugin
    for (int i = _flyViewPanelItems.size() - 1; i >= 0; --i) {
        QVariantMap item = _flyViewPanelItems[i].toMap();
        if (item["name"].toString() == pluginName) {
            _flyViewPanelItems.removeAt(i);
        }
    }
    emit flyViewPanelItemsChanged();
}

void QGCPluginManager::unloadPlugin(const QString& pluginName)
{
    qCDebug(QGCPluginManagerLog) << "Unloading plugin:" << pluginName;
    
    // Find and remove the plugin
    for (int i = 0; i < _loadedPluginInfos.size(); ++i) {
        if (_loadedPluginInfos[i].name == pluginName) {
            PluginInfo info = _loadedPluginInfos[i];
            
            // Cleanup and delete the plugin
            if (info.plugin) {
                info.plugin->cleanup();
                delete info.plugin;
            }
            
            // Remove from list
            _loadedPluginInfos.removeAt(i);

            // Recalculate replay extension in case the unloaded plugin owned it
            QGCReplayExtension* newExt = nullptr;
            for (const PluginInfo& pi : _loadedPluginInfos) {
                newExt = pi.plugin->replayExtension();
                if (newExt) break;
            }
            if (_replayExtension != newExt) {
                _replayExtension = newExt;
                emit replayExtensionChanged();
            }

            // Remove associated menu items
            _removeToolMenuItemsForPlugin(pluginName);

            emit loadedPluginsChanged();
            qCDebug(QGCPluginManagerLog) << "Plugin unloaded:" << pluginName;
            return;
        }
    }
    
    qCWarning(QGCPluginManagerLog) << "Plugin not found for unload:" << pluginName;
}

void QGCPluginManager::reloadPlugin(const QString& pluginName)
{
    qCDebug(QGCPluginManagerLog) << "Reloading plugin:" << pluginName;
    
    // Find the plugin if currently loaded (to get its path)
    QString pluginPath;
    for (const PluginInfo& info : _loadedPluginInfos) {
        if (info.name == pluginName) {
            pluginPath = info.path;
            qCDebug(QGCPluginManagerLog) << "Found plugin path:" << pluginPath;
            // Unload it now
            unloadPlugin(pluginName);
            break;
        }
    }
    
    // If we have a specific path, load directly from it
    // Otherwise fall back to scanning all directories (for newly added plugins)
    QGCPluginLoader loader(this);
    
    if (!pluginPath.isEmpty()) {
        qCDebug(QGCPluginManagerLog) << "Loading plugin from stored path:" << pluginPath;
        PluginLoadInfo loadInfo = loader.loadPlugin(pluginPath);
        
        if (loadInfo.plugin && loadInfo.plugin->name() == pluginName) {
            _addLoadedPlugin(loadInfo);
            qCDebug(QGCPluginManagerLog) << "Plugin reloaded successfully from path:" << pluginName;
            return;
        } else {
            qCWarning(QGCPluginManagerLog) << "Failed to reload plugin from stored path:" << pluginPath;
        }
    }
    
    // Fall back to scanning all directories
    qCDebug(QGCPluginManagerLog) << "Scanning all plugin directories for:" << pluginName;
    QStringList pluginPaths = QGCPluginLoader::defaultPluginPaths();
    loader.loadPlugins(pluginPaths);
    
    QList<PluginLoadInfo> pluginInfos = loader.loadedPluginInfos();
    
    // Find the plugin we want to reload
    for (const PluginLoadInfo& loadInfo : pluginInfos) {
        if (loadInfo.plugin->name() == pluginName) {
            qCDebug(QGCPluginManagerLog) << "Found plugin in scan:" << pluginName;
            _addLoadedPlugin(loadInfo);
            
            // Cleanup other plugins we don't want
            for (const PluginLoadInfo& otherInfo : pluginInfos) {
                if (otherInfo.plugin != loadInfo.plugin) {
                    otherInfo.plugin->cleanup();
                    delete otherInfo.plugin;
                }
            }
            
            qCDebug(QGCPluginManagerLog) << "Plugin reloaded successfully from scan:" << pluginName;
            return;
        }
    }
    
    // Cleanup all plugins since we didn't find what we wanted
    for (const PluginLoadInfo& info : pluginInfos) {
        info.plugin->cleanup();
        delete info.plugin;
    }
    
    qCWarning(QGCPluginManagerLog) << "Failed to reload plugin - not found in scan:" << pluginName;
}

void QGCPluginManager::_addLoadedPlugin(const PluginLoadInfo& loadInfo)
{
    QGCPlugin* plugin = loadInfo.plugin;
    QString pluginName = plugin->name();
    
    // Check if already loaded (defensive)
    for (const PluginInfo& info : _loadedPluginInfos) {
        if (info.name == pluginName) {
            qCWarning(QGCPluginManagerLog) << "Plugin already loaded:" << pluginName;
            return;
        }
    }
    
    // Create plugin info structure
    PluginInfo info;
    info.plugin = plugin;
    info.name = pluginName;
    info.path = loadInfo.filePath;
    
    _loadedPluginInfos.append(info);
    
    // Initialize the plugin
    plugin->init();

    // Register replay extension if this plugin provides one and none is set yet
    if (!_replayExtension) {
        QGCReplayExtension* ext = plugin->replayExtension();
        if (ext) {
            _replayExtension = ext;
            emit replayExtensionChanged();
        }
    }

    // Add tool menu item
    QVariantMap menuItem = plugin->toolMenuItem();
    if (!menuItem.isEmpty()) {
        menuItem["pluginName"] = pluginName;
        addToolMenuItem(menuItem);
        qCDebug(QGCPluginManagerLog) << "Added menu item for plugin:" << menuItem["title"];
    }

    // Register fly-view panel item if this plugin provides one
    QString panelUrl = plugin->flyViewPanelUrl();
    if (!panelUrl.isEmpty()) {
        QPointF defaultPos = plugin->flyViewPanelDefaultPosition();
        QVariantMap panelItem;
        panelItem["name"]             = pluginName;
        panelItem["panelUrl"]         = panelUrl;
        panelItem["dockUrl"]          = plugin->flyViewPanelDockUrl();
        panelItem["defaultWidth"]     = plugin->flyViewPanelDefaultWidth();
        panelItem["defaultHeight"]    = plugin->flyViewPanelDefaultHeight();
        panelItem["defaultXFraction"] = defaultPos.x();
        panelItem["defaultYFraction"] = defaultPos.y();
        _flyViewPanelItems.append(panelItem);
        emit flyViewPanelItemsChanged();
        qCDebug(QGCPluginManagerLog) << "Added fly-view panel for plugin:" << pluginName;
    }

    emit loadedPluginsChanged();
}
