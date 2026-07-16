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
#include "PluginInstaller.h"
#include "PluginSettings.h"
#include "Fact.h"
#include "HostServices/QGCAppServiceImpl.h"
#include "HostServices/QGCHostServicesImpl.h"
#include "HostServices/QGCMissionServiceImpl.h"
#include "HostServices/QGCReplayServiceImpl.h"
#include "HostServices/QGCTelemetryLoggingServiceImpl.h"
#include "HostServices/QGCVehicleServiceImpl.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
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
    case PluginState::NeedsApproval:
        return QStringLiteral("NeedsApproval");
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
        if (record.state == PluginState::Active && record.contributions.controlsTelemetryLogging) {
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
        if (record.plugin && record.contributions.providesReplayExtension) {
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
        info["tier"]        = PluginManifest::tierToString(record.manifest.tier);
        info["state"]       = pluginStateName(record.state);
        info["statusText"]  = _statusText(record);
        // Only a package installed under the user plugins directory can be removed
        // through the settings page; bundle-shipped and dev-loop bare dylibs cannot.
        // QDir::filePath(id) is exactly how PluginInstaller lays packages out, so a
        // direct parent-path comparison (not a string-prefix check, which could match
        // an unrelated sibling directory sharing a prefix) is both correct and simple.
        const QString userPluginsDir = PluginInstaller::userPluginsDir();
        info["removable"]   = !record.packageDir.isEmpty() && !userPluginsDir.isEmpty()
            && QFileInfo(record.packageDir).dir().absolutePath() == QDir(userPluginsDir).absolutePath();
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
    case PluginState::NeedsApproval:
        return tr("Downloaded plugin — approve to run");
    case PluginState::Discovered:
        return tr("Pending");
    }
    return tr("Unknown");
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

void QGCPluginManager::_applyQuarantineGate(PluginLoadInfo& record)
{
#if defined(Q_OS_MACOS)
    // Manually-installed packages (dropped into the plugins dir rather than installed
    // via installPlugin()) may still carry com.apple.quarantine from however they
    // arrived. Packages installFromFile() extracts are never quarantined (01 §1.4);
    // D10's general "every user-dir plugin starts unapproved" rule is U3.3 — this is
    // the narrower quarantine-specific gate U3.2 needs on its own.
    if (record.state == PluginState::Discovered
        && !record.packageDir.isEmpty()
        && PluginInstaller::isQuarantined(record.packageDir)) {
        record.state = PluginState::NeedsApproval;
        record.errorString = QStringLiteral("downloaded plugin — approve to run");
    }
#else
    Q_UNUSED(record);
#endif
}

void QGCPluginManager::_activateIfEnabled(PluginLoadInfo& record)
{
    if (SettingsManager::instance()->pluginSettings()->isPluginEnabled(record.manifest.id)) {
        _activateRecord(record);
    } else {
        record.state = PluginState::Disabled;
    }
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

        _applyQuarantineGate(record);

        if (record.state == PluginState::Discovered) {
            _activateIfEnabled(record);
        } else if (record.state == PluginState::NeedsApproval) {
            qCDebug(QGCPluginManagerLog) << "  - Awaiting approval (quarantined):" << pluginId;
        } else {
            qCWarning(QGCPluginManagerLog) << "  - Not activating:" << record.errorString;
        }

        _records.append(record);
    }

    _recalcLoggingController();
    emit loadedPluginsChanged();
}

void QGCPluginManager::_ensureHostServices()
{
    if (_hostServices) {
        return;
    }

    // Built on first activation (still before the QML engine exists) so paths
    // that never activate a plugin — disabled sets, unit tests with manifest
    // fixtures — don't touch the wrapped singletons.
    _hostServices = new QGCHostServicesImpl(this);
    _hostServices->registerService(QGCReplayServiceId, new QGCReplayServiceImpl(_hostServices));
    _hostServices->registerService(QGCTelemetryLoggingServiceId, new QGCTelemetryLoggingServiceImpl(_hostServices));
    _hostServices->registerService(QGCVehicleServiceId, new QGCVehicleServiceImpl(_hostServices));
    _hostServices->registerService(QGCMissionServiceId, new QGCMissionServiceImpl(_hostServices));
    _hostServices->registerService(QGCAppServiceId, new QGCAppServiceImpl(_hostServices));
    qCDebug(QGCPluginManagerLog) << "Host services ready:"
        << QGCReplayServiceId << QGCTelemetryLoggingServiceId
        << QGCVehicleServiceId << QGCMissionServiceId << QGCAppServiceId;
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

    // Tier qml packages have no binary — nothing to init() or query for a replay
    // extension; their contributions came entirely from the manifest (D1).
    if (plugin) {
        _ensureHostServices();
        plugin->init(_hostServices);

        // Register the replay extension when the manifest declares one; undeclared
        // extensions are never queried (the manifest is the contract)
        if (record.contributions.providesReplayExtension) {
            QGCReplayExtension* ext = plugin->replayExtension();
            if (!ext) {
                qCWarning(QGCPluginManagerLog) << "Plugin" << pluginId
                    << "declares a replay extension in its manifest but provides none";
            } else if (!_replayExtension) {
                _replayExtension = ext;
                emit replayExtensionChanged();
            } else {
                qCWarning(QGCPluginManagerLog) << "Plugin" << pluginId
                    << "provides a replay extension, but one is already registered by another plugin"
                    << "- ignoring (first registration wins)";
            }
        }
    }

    _addContributions(record);
}

void QGCPluginManager::_addContributions(const PluginLoadInfo& record)
{
    const PluginContributions& contributions = record.contributions;

    if (!contributions.toolMenuItem.isEmpty()) {
        qCDebug(QGCPluginManagerLog) << "  - Provides menu item:" << contributions.toolMenuItem["title"];
        _toolMenuItems.append(contributions.toolMenuItem);
        emit toolMenuItemsChanged();
    }

    if (!contributions.flyViewPanelItem.isEmpty()) {
        qCDebug(QGCPluginManagerLog) << "  - Provides fly-view panel:" << contributions.flyViewPanelItem["panelUrl"];
        _flyViewPanelItems.append(contributions.flyViewPanelItem);
        emit flyViewPanelItemsChanged();
    }

    if (!contributions.planViewPanelItem.isEmpty()) {
        qCDebug(QGCPluginManagerLog) << "  - Provides plan-view panel:" << contributions.planViewPanelItem["panelUrl"];
        _planViewPanelItems.append(contributions.planViewPanelItem);
        emit planViewPanelItemsChanged();
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

    // Re-inspect the stored path only: the manifest may have changed on disk. A
    // package re-inspects via its directory (the sidecar qgcplugin.json governs
    // identity/contributions), not the resolved binary path inspect() expects.
    PluginLoadInfo fresh = record->packageDir.isEmpty()
        ? QGCPluginLoader::inspect(record->filePath)
        : QGCPluginLoader::inspectPackage(record->packageDir);
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
        _applyQuarantineGate(fresh);

        if (fresh.state == PluginState::Discovered) {
            _activateIfEnabled(fresh);
        } else if (fresh.state != PluginState::NeedsApproval) {
            qCWarning(QGCPluginManagerLog) << "Reload inspection failed:" << record->filePath << "-" << fresh.errorString;
        }
        *record = fresh;
    }

    _recalcLoggingController();
    emit loadedPluginsChanged();
}

QString QGCPluginManager::installPlugin(const QString& zipPath)
{
    qCDebug(QGCPluginManagerLog) << "Installing plugin from:" << zipPath;

    const PluginInstallResult installResult = PluginInstaller::installFromFile(zipPath);
    if (!installResult.success) {
        qCWarning(QGCPluginManagerLog) << "Install failed:" << installResult.errorString;
        return installResult.errorString;
    }

    // Replacing an existing install: drop the old record (deactivating first) so the
    // freshly-inspected one below isn't rejected as a duplicate id.
    PluginLoadInfo* existing = _findRecord(installResult.pluginId);
    if (existing) {
        if (existing->state == PluginState::Active) {
            _deactivateRecord(*existing);
        }
        _records.removeIf([&installResult](const PluginLoadInfo& r) {
            return r.manifest.id == installResult.pluginId;
        });
    }

    const QString packageDir = QDir(PluginInstaller::userPluginsDir()).filePath(installResult.pluginId);
    _processInspected({QGCPluginLoader::inspectPackage(packageDir)});

    return QString();
}

QString QGCPluginManager::removePlugin(const QString& pluginId)
{
    qCDebug(QGCPluginManagerLog) << "Removing plugin:" << pluginId;

    PluginLoadInfo* record = _findRecord(pluginId);

    // A loaded plugin's binary must be deactivated before its files can be deleted
    // (mapped-in-process dylibs can't be removed on some platforms while active).
    const bool wasActive = record && record->state == PluginState::Active;
    if (wasActive) {
        _deactivateRecord(*record);
    }

    const PluginInstallResult removeResult = PluginInstaller::removePlugin(pluginId);
    if (!removeResult.success) {
        qCWarning(QGCPluginManagerLog) << "Remove failed:" << removeResult.errorString;
        // Deletion didn't happen: put the plugin back the way it was rather than
        // leaving it disabled with no recorded reason.
        if (wasActive && record) {
            _activateRecord(*record);
            _recalcLoggingController();
            emit loadedPluginsChanged();
        }
        return removeResult.errorString;
    }

    _records.removeIf([&pluginId](const PluginLoadInfo& r) {
        return r.manifest.id == pluginId;
    });
    emit loadedPluginsChanged();

    return QString();
}

void QGCPluginManager::approvePlugin(const QString& pluginId)
{
    qCDebug(QGCPluginManagerLog) << "Approving plugin:" << pluginId;

    PluginLoadInfo* record = _findRecord(pluginId);
    if (!record || record->state != PluginState::NeedsApproval) {
        qCWarning(QGCPluginManagerLog) << "Plugin not awaiting approval:" << pluginId;
        return;
    }

#if defined(Q_OS_MACOS)
    if (!PluginInstaller::stripQuarantine(record->packageDir)) {
        qCWarning(QGCPluginManagerLog) << "Could not fully strip quarantine from" << pluginId << "- leaving unapproved";
        return;
    }
#endif

    record->state = PluginState::Discovered;
    record->errorString.clear();

    _activateIfEnabled(*record);

    _recalcLoggingController();
    emit loadedPluginsChanged();
}
