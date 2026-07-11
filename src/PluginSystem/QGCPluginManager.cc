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
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "PluginSettings.h"
#include "Fact.h"

#include <QtCore/QApplicationStatic>
#include <QtQml/qqml.h>

QGC_LOGGING_CATEGORY(QGCPluginManagerLog, "PluginSystem.QGCPluginManager");

namespace {

QString pluginStateName(PluginState state)
{
    switch (state) {
    case PluginState::Discovered:
        return QStringLiteral("Discovered");
    case PluginState::Incompatible:
        return QStringLiteral("Incompatible");
    case PluginState::Disabled:
        return QStringLiteral("Disabled");
    case PluginState::Active:
        return QStringLiteral("Active");
    case PluginState::Failed:
        return QStringLiteral("Failed");
    case PluginState::Quarantined:
        return QStringLiteral("Quarantined");
    }
    return QStringLiteral("Failed");
}

} // namespace

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
    for (const PluginLoadInfo& record : _records) {
        if (record.plugin) {
            record.plugin->cleanup();
            delete record.plugin;
        }
    }
    _records.clear();
    _toolMenuItems.clear();
    if (_replayExtension) {
        _replayExtension = nullptr;
        emit replayExtensionChanged();
    }
    _flyViewPanelItems.clear();
    _planViewPanelItems.clear();
    _hasLoggingController = false;
    emit flyViewPanelItemsChanged();
    emit planViewPanelItemsChanged();
    emit loadedPluginsChanged();
    emit toolMenuItemsChanged();
}

void QGCPluginManager::_recalcLoggingController()
{
    bool found = false;
    for (const PluginLoadInfo& record : _records) {
        if (record.plugin && record.plugin->controlsTelemetryLogging()) {
            found = true;
            break;
        }
    }
    _hasLoggingController = found;
}

void QGCPluginManager::_recalcReplayExtension()
{
    QGCReplayExtension* newExt = nullptr;
    for (const PluginLoadInfo& record : _records) {
        if (record.plugin) {
            newExt = record.plugin->replayExtension();
            if (newExt) {
                break;
            }
        }
    }
    if (_replayExtension != newExt) {
        _replayExtension = newExt;
        emit replayExtensionChanged();
    }
}

QVariantList QGCPluginManager::loadedPlugins() const
{
    QVariantList pluginList;
    for (const PluginLoadInfo& record : _records) {
        if (record.state == PluginState::Active) {
            QVariantMap pluginInfo;
            pluginInfo["name"] = record.manifest.name;
            pluginList.append(pluginInfo);
        }
    }
    return pluginList;
}

QVariantList QGCPluginManager::knownPlugins() const
{
    QVariantList pluginList;
    for (const PluginLoadInfo& record : _records) {
        QVariantMap info;
        info["id"]          = record.manifest.id;
        info["name"]        = record.manifest.name;
        info["version"]     = record.manifest.version.toString();
        info["vendor"]      = record.manifest.vendor;
        info["description"] = record.manifest.description;
        info["state"]       = pluginStateName(record.state);
        info["statusText"]  = _statusText(record);
        pluginList.append(info);
    }
    return pluginList;
}

QString QGCPluginManager::_statusText(const PluginLoadInfo& record) const
{
    switch (record.state) {
    case PluginState::Active:
        return tr("Active");
    case PluginState::Disabled:
        return tr("Disabled");
    case PluginState::Incompatible:
        return tr("Incompatible: %1").arg(record.errorString);
    case PluginState::Failed:
        return tr("Failed to load: %1").arg(record.errorString);
    case PluginState::Quarantined:
        return tr("Quarantined: %1").arg(record.errorString);
    case PluginState::Discovered:
        return tr("Pending");
    }
    return tr("Unknown");
}

void QGCPluginManager::addToolMenuItem(const QVariantMap& item)
{
    _toolMenuItems.append(item);
    emit toolMenuItemsChanged();
}

PluginLoadInfo* QGCPluginManager::_findRecord(const QString& pluginId)
{
    for (PluginLoadInfo& record : _records) {
        if (record.manifest.id == pluginId) {
            return &record;
        }
    }
    return nullptr;
}

void QGCPluginManager::_loadPlugins()
{
    qCDebug(QGCPluginManagerLog) << "=== Plugin Loading Start ===";

    const QStringList pluginPaths = QGCPluginLoader::defaultPluginPaths();
    qCDebug(QGCPluginManagerLog) << "Plugin search paths:" << pluginPaths;

    _processInspected(QGCPluginLoader::inspectDirectories(pluginPaths));

    qCDebug(QGCPluginManagerLog) << "=== Plugin Loading Complete:" << loadedPlugins().size() << "plugin(s) active ===";
}

void QGCPluginManager::_processInspected(const QList<PluginLoadInfo>& infos)
{
    PluginSettings* pluginSettings = SettingsManager::instance()->pluginSettings();

    for (const PluginLoadInfo& info : infos) {
        PluginLoadInfo record = info;
        const QString pluginId = record.manifest.id;
        qCDebug(QGCPluginManagerLog) << "Processing plugin:" << pluginId << "from" << record.filePath;

        if (!pluginId.isEmpty() && _findRecord(pluginId)) {
            record.state = PluginState::Failed;
            record.errorString = QStringLiteral("duplicate plugin id: %1").arg(pluginId);
            qCWarning(QGCPluginManagerLog) << "  -" << record.errorString << "(" << record.filePath << ")";
            _records.append(record);
            continue;
        }

        // Failed records without a readable manifest have no id to key settings by
        if (!pluginId.isEmpty()) {
            // Interim default until the trust model lands: the Example plugin is
            // disabled by default, everything else is enabled
            const bool defaultEnabled = (pluginId != QStringLiteral("org.qgroundcontrol.example"));
            pluginSettings->registerPlugin(pluginId, record.manifest.name, defaultEnabled);
        }

        if (record.state == PluginState::Discovered) {
            if (pluginSettings->isPluginEnabled(pluginId)) {
                _activateRecord(record);
            } else {
                // Never activated: the plugin's code does not run
                qCDebug(QGCPluginManagerLog) << "  - Skipping disabled plugin";
                record.state = PluginState::Disabled;
            }
        } else {
            qCWarning(QGCPluginManagerLog) << "  - Not activating:" << record.errorString;
        }

        _records.append(record);
    }

    _recalcLoggingController();
    emit loadedPluginsChanged();
}

void QGCPluginManager::_activateRecord(PluginLoadInfo& record)
{
    QGCPluginLoader::activate(record);

    if (record.state != PluginState::Active) {
        qCWarning(QGCPluginManagerLog) << "Failed to activate plugin:" << record.filePath << "-" << record.errorString;
        return;
    }

    QGCPlugin* plugin = record.plugin;
    const QString pluginId = record.manifest.id;
    const QString displayName = record.manifest.name;

    // Initialize the plugin. The host does not implement any services yet, so
    // plugins receive a null services handle (allowed by the init() contract).
    plugin->init(nullptr);

    // Register replay extension if this plugin provides one and none is set yet
    if (!_replayExtension) {
        QGCReplayExtension* ext = plugin->replayExtension();
        if (ext) {
            _replayExtension = ext;
            emit replayExtensionChanged();
        }
    } else if (plugin->replayExtension()) {
        qCWarning(QGCPluginManagerLog) << "Plugin" << pluginId
            << "provides a replay extension, but one is already registered by another plugin"
            << "- ignoring (first registration wins)";
    }

    // Get plugin's tool menu item and add it
    QVariantMap menuItem = plugin->toolMenuItem();
    if (!menuItem.isEmpty()) {
        qCDebug(QGCPluginManagerLog) << "  - Provides menu item:" << menuItem["title"];
        menuItem["pluginId"] = pluginId;
        addToolMenuItem(menuItem);
    }

    // Register fly-view panel item if this plugin provides one
    QString panelUrl = plugin->flyViewPanelUrl();
    if (!panelUrl.isEmpty()) {
        QPointF defaultPos = plugin->flyViewPanelDefaultPosition();
        QVariantMap panelItem;
        panelItem["pluginId"]         = pluginId;
        panelItem["name"]             = displayName;
        panelItem["panelUrl"]         = panelUrl;
        panelItem["dockUrl"]          = plugin->flyViewPanelDockUrl();
        panelItem["defaultWidth"]     = plugin->flyViewPanelDefaultWidth();
        panelItem["defaultHeight"]    = plugin->flyViewPanelDefaultHeight();
        panelItem["defaultXFraction"] = defaultPos.x();
        panelItem["defaultYFraction"] = defaultPos.y();
        _flyViewPanelItems.append(panelItem);
        emit flyViewPanelItemsChanged();
        qCDebug(QGCPluginManagerLog) << "  - Provides fly-view panel:" << panelUrl;
    }

    // Register plan-view panel item if this plugin provides one
    QString planPanelUrl = plugin->planViewPanelUrl();
    if (!planPanelUrl.isEmpty()) {
        QPointF defaultPos = plugin->planViewPanelDefaultPosition();
        QVariantMap panelItem;
        panelItem["pluginId"]         = pluginId;
        panelItem["name"]             = displayName;
        panelItem["panelUrl"]         = planPanelUrl;
        panelItem["dockUrl"]          = plugin->planViewPanelDockUrl();
        panelItem["defaultWidth"]     = plugin->planViewPanelDefaultWidth();
        panelItem["defaultHeight"]    = plugin->planViewPanelDefaultHeight();
        panelItem["defaultXFraction"] = defaultPos.x();
        panelItem["defaultYFraction"] = defaultPos.y();
        _planViewPanelItems.append(panelItem);
        emit planViewPanelItemsChanged();
        qCDebug(QGCPluginManagerLog) << "  - Provides plan-view panel:" << planPanelUrl;
    }
}

void QGCPluginManager::_deactivateRecord(PluginLoadInfo& record)
{
    if (record.plugin) {
        record.plugin->cleanup();
        delete record.plugin;
        record.plugin = nullptr;
    }

    // The library mapping stays; a full drop happens on restart
    record.state = PluginState::Disabled;

    _removeContributionsForPlugin(record.manifest.id);
    _recalcReplayExtension();
    _recalcLoggingController();
    emit loadedPluginsChanged();
}

void QGCPluginManager::_removeContributionsForPlugin(const QString& pluginId)
{
    // Remove all tool menu items for this plugin
    for (int i = _toolMenuItems.size() - 1; i >= 0; --i) {
        if (_toolMenuItems[i].toMap()["pluginId"].toString() == pluginId) {
            _toolMenuItems.removeAt(i);
        }
    }
    emit toolMenuItemsChanged();

    // Remove fly-view panel item for this plugin
    for (int i = _flyViewPanelItems.size() - 1; i >= 0; --i) {
        if (_flyViewPanelItems[i].toMap()["pluginId"].toString() == pluginId) {
            _flyViewPanelItems.removeAt(i);
        }
    }
    emit flyViewPanelItemsChanged();

    // Remove plan-view panel item for this plugin
    for (int i = _planViewPanelItems.size() - 1; i >= 0; --i) {
        if (_planViewPanelItems[i].toMap()["pluginId"].toString() == pluginId) {
            _planViewPanelItems.removeAt(i);
        }
    }
    emit planViewPanelItemsChanged();
}

void QGCPluginManager::setPluginEnabled(const QString& pluginId, bool enabled)
{
    PluginLoadInfo* record = _findRecord(pluginId);
    if (!record) {
        qCWarning(QGCPluginManagerLog) << "Plugin not found:" << pluginId;
        return;
    }

    // Persist the setting; QML sliders may already have written it, which is fine
    PluginSettings* pluginSettings = SettingsManager::instance()->pluginSettings();
    Fact* fact = pluginSettings->pluginEnabledFact(pluginId);
    if (fact && fact->rawValue().toBool() != enabled) {
        fact->setRawValue(enabled);
    }

    if (enabled) {
        if (record->state == PluginState::Disabled || record->state == PluginState::Discovered) {
            qCDebug(QGCPluginManagerLog) << "Enabling plugin:" << pluginId;
            _activateRecord(*record);
            _recalcLoggingController();
            emit loadedPluginsChanged();
        }
    } else {
        if (record->state == PluginState::Active) {
            qCDebug(QGCPluginManagerLog) << "Disabling plugin:" << pluginId;
            _deactivateRecord(*record);
        } else if (record->state == PluginState::Discovered) {
            record->state = PluginState::Disabled;
        }
    }
}

void QGCPluginManager::reloadPlugin(const QString& pluginId)
{
    qCDebug(QGCPluginManagerLog) << "Reloading plugin:" << pluginId;

    PluginLoadInfo* record = _findRecord(pluginId);
    if (!record) {
        qCWarning(QGCPluginManagerLog) << "Plugin not found for reload:" << pluginId;
        return;
    }

    if (record->state == PluginState::Active) {
        _deactivateRecord(*record);
    }

    // Re-inspect the stored path only: the manifest may have changed on disk
    PluginLoadInfo fresh = QGCPluginLoader::inspect(record->filePath);
    fresh.plugin = nullptr;

    if (fresh.manifest.id != pluginId) {
        // The file no longer declares this plugin (id changed or manifest unreadable).
        // Keep the record's identity so the settings key and id lookups stay coherent;
        // a plugin with a new id is picked up on restart.
        record->state = PluginState::Failed;
        record->errorString = fresh.manifest.id.isEmpty()
            ? fresh.errorString
            : QStringLiteral("plugin id changed on disk (now %1); restart to load it").arg(fresh.manifest.id);
        qCWarning(QGCPluginManagerLog) << "Reload inspection failed:" << record->filePath << "-" << record->errorString;
    } else {
        if (fresh.state == PluginState::Discovered) {
            if (SettingsManager::instance()->pluginSettings()->isPluginEnabled(pluginId)) {
                _activateRecord(fresh);
            } else {
                fresh.state = PluginState::Disabled;
            }
        } else {
            qCWarning(QGCPluginManagerLog) << "Reload inspection failed:" << record->filePath << "-" << fresh.errorString;
        }
        *record = fresh;
    }

    _recalcLoggingController();
    emit loadedPluginsChanged();
}
