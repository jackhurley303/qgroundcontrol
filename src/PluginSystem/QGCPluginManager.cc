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
#include "PluginInstaller.h"
#include "Fact.h"
#include "HostServices/QGCAppServiceImpl.h"
#include "HostServices/QGCHostServicesImpl.h"
#include "HostServices/QGCMissionServiceImpl.h"
#include "HostServices/QGCParameterServiceImpl.h"
#include "HostServices/QGCReplayServiceImpl.h"
#include "HostServices/QGCTelemetryLoggingServiceImpl.h"
#include "HostServices/QGCVehicleServiceImpl.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtQml/qqml.h>

QGC_LOGGING_CATEGORY(QGCPluginManagerLog, "PluginSystem.QGCPluginManager");

namespace {

// A record's container is the entry the search directory scan found: the package
// directory, or the bare dylib file itself. Its parent being the user plugins dir is
// what makes a plugin user-dir (untrusted until approved, D10). Direct parent-path
// comparison, not a string-prefix check — same rationale as knownPlugins().
bool isUserDirPlugin(const PluginLoadInfo& record)
{
    const QString userPluginsDir = PluginInstaller::userPluginsDir();
    if (userPluginsDir.isEmpty()) {
        return false;
    }
    const QString container = record.packageDir.isEmpty() ? record.filePath : record.packageDir;
    return !container.isEmpty()
        && QFileInfo(container).dir().absolutePath() == QDir(userPluginsDir).absolutePath();
}

QString packageManifestPath(const PluginLoadInfo& record)
{
    return QDir(record.packageDir).filePath(QStringLiteral("qgcplugin.json"));
}

// Consent digest for a user-dir plugin: manifest version + SHA-256 over the manifest
// file and the resolved binary (R3's bounded scope — the same two files whose
// quarantine status matters, D15). This deliberately does NOT cover a package's QML/
// asset tree: those files are code too (a plugin's QML can run JS and call host
// services), but hashing an arbitrary-depth tree on every startup was decided against
// as disproportionate to the risk (R3) — swapping only the QML in an already-approved
// package is a residual gap, not an oversight. A bare dylib embeds its manifest, so the
// binary alone covers both; a qml-tier package has no binary, so the manifest alone
// does. Empty on read failure, which callers must treat as "cannot consent".
QString consentDigest(const PluginLoadInfo& record)
{
    QStringList files;
    if (!record.packageDir.isEmpty()) {
        files << packageManifestPath(record);
    }
    if (record.filePath != record.packageDir) {
        files << record.filePath;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& filePath : files) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
            return QString();
        }
    }
    return record.manifest.version.toString() + QLatin1Char(':') + QString::fromLatin1(hash.result().toHex());
}

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
    for (const PluginLoadInfo& record : _recordStore.records()) {
        if (record.plugin) {
            record.plugin->cleanup();
            delete record.plugin;
        }
    }
    _recordStore.clear();
    _toolMenuItems.clear();
    _flyViewPanelItems.clear();
    _planViewPanelItems.clear();
    emit flyViewPanelItemsChanged();
    emit planViewPanelItemsChanged();
    _notifyRecordsChanged();
    emit toolMenuItemsChanged();
}

void QGCPluginManager::_recalcLoggingController()
{
    bool found = false;
    for (const PluginLoadInfo& record : _recordStore.records()) {
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
    for (const PluginLoadInfo& record : _recordStore.records()) {
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

void QGCPluginManager::_notifyRecordsChanged()
{
    _recalcReplayExtension();
    _recalcLoggingController();
    emit loadedPluginsChanged();
}

QVariantList QGCPluginManager::loadedPlugins() const
{
    QVariantList pluginList;
    for (const PluginLoadInfo& record : _recordStore.records()) {
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
    for (const PluginLoadInfo& record : _recordStore.records()) {
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
        // The trust gate records why (new / changed / downloaded), already user-facing
        return record.errorString.isEmpty() ? tr("Approval needed to run") : record.errorString;
    case PluginState::Discovered:
        return tr("Pending");
    }
    return tr("Unknown");
}

void QGCPluginManager::_applyTrustGate(PluginLoadInfo& record)
{
    if (record.state != PluginState::Discovered) {
        return;
    }

    // Crash sentinel: the last run died inside this plugin's activation. Checked
    // first — it outranks NeedsApproval, because a plugin that crashed the host must
    // not become runnable by mere consent. Only an explicit re-enable
    // (setPluginEnabled) clears the marker.
    const QString crashedPluginId = _recordStore.crashedPluginId();
    if (!crashedPluginId.isEmpty() && record.manifest.id == crashedPluginId) {
        record.state = PluginState::Quarantined;
        record.errorString = tr("QGC crashed while loading this plugin last run — re-enable to retry");
        return;
    }

#if defined(Q_OS_MACOS)
    // Manually-dropped plugins (not extracted in-process by installFromFile(), 01 §1.4)
    // may carry com.apple.quarantine from however they arrived. Check the manifest and
    // the resolved binary (D15): manifest-only would miss a fresh quarantined dylib
    // swapped into an otherwise clean package, which Gatekeeper then kills cryptically
    // at dlopen. Applies in every search dir — quarantine means "downloaded", wherever
    // the file was dropped.
    bool quarantined = false;
    if (!record.packageDir.isEmpty()) {
        quarantined = PluginInstaller::isFileQuarantined(packageManifestPath(record));
    }
    if (!quarantined && record.filePath != record.packageDir) {
        quarantined = PluginInstaller::isFileQuarantined(record.filePath);
    }
    if (quarantined) {
        record.state = PluginState::NeedsApproval;
        record.errorString = tr("Downloaded plugin — approve to run");
        return;
    }
#endif

    // D10: bundle-dir plugins are trusted; a user-dir plugin runs only with recorded
    // consent, keyed to its content — a changed plugin must re-prompt.
    if (isUserDirPlugin(record)) {
        const QString approved = _recordStore.approvedPluginDigest(record.manifest.id);
        const QString digest = consentDigest(record);
        if (digest.isEmpty() || digest != approved) {
            record.state = PluginState::NeedsApproval;
            record.errorString = approved.isEmpty()
                ? tr("New plugin — approve to run")
                : tr("Plugin changed since approval — approve again to run");
        }
    }
}

void QGCPluginManager::_activateIfEnabled(PluginLoadInfo& record)
{
    if (_recordStore.isPluginEnabled(record.manifest.id)) {
        _activateRecord(record);
    } else {
        record.state = PluginState::Disabled;
    }
}

void QGCPluginManager::_loadPlugins()
{
    qCDebug(QGCPluginManagerLog) << "=== Plugin Loading Start ===";

    _recordStore.checkCrashSentinel();

    const QStringList pluginPaths = QGCPluginLoader::defaultPluginPaths();
    qCDebug(QGCPluginManagerLog) << "Plugin search paths:" << pluginPaths;

    _processInspected(QGCPluginLoader::inspectDirectories(pluginPaths));

    qCDebug(QGCPluginManagerLog) << "=== Plugin Loading Complete:" << loadedPlugins().size() << "plugin(s) active ===";
}

void QGCPluginManager::_processInspected(const QList<PluginLoadInfo>& infos)
{
    for (const PluginLoadInfo& info : infos) {
        PluginLoadInfo record = info;
        const QString pluginId = record.manifest.id;
        qCDebug(QGCPluginManagerLog) << "Processing plugin:" << pluginId << "from" << record.filePath;

        if (!pluginId.isEmpty() && _recordStore.find(pluginId)) {
            record.state = PluginState::Failed;
            record.errorString = QStringLiteral("duplicate plugin id: %1").arg(pluginId);
            qCWarning(QGCPluginManagerLog) << "  -" << record.errorString << "(" << record.filePath << ")";
            _recordStore.append(record);
            continue;
        }

        // Failed records without a readable manifest have no id to key settings by.
        // Enabled by default across the board: what gates an untrusted plugin is the
        // consent model (_applyTrustGate, D10), not the enabled Fact.
        if (!pluginId.isEmpty()) {
            _recordStore.registerPlugin(pluginId, record.manifest.name, true);
        }

        _applyTrustGate(record);

        if (record.state == PluginState::Discovered) {
            _activateIfEnabled(record);
        } else if (record.state == PluginState::NeedsApproval) {
            qCDebug(QGCPluginManagerLog) << "  - Awaiting approval:" << pluginId << "-" << record.errorString;
        } else {
            qCWarning(QGCPluginManagerLog) << "  - Not activating:" << record.errorString;
        }

        _recordStore.append(record);
    }

    _notifyRecordsChanged();
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
    _hostServices->registerService(QGCParameterServiceId, new QGCParameterServiceImpl(_hostServices));
    _hostServices->registerService(QGCAppServiceId, new QGCAppServiceImpl(_hostServices));
    qCDebug(QGCPluginManagerLog) << "Host services ready:"
        << QGCReplayServiceId << QGCTelemetryLoggingServiceId
        << QGCVehicleServiceId << QGCMissionServiceId << QGCParameterServiceId << QGCAppServiceId;
}

void QGCPluginManager::_activateRecord(PluginLoadInfo& record)
{
    // Crash sentinel: if the process dies anywhere inside this activation (dlopen,
    // static initializers, init(), contribution wiring), the synced id lingers and
    // the next boot quarantines the plugin instead of crash-looping.
    const auto clearSentinel = _recordStore.armLoadingSentinel(record.manifest.id);

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

        // Diagnostics only — registering _replayExtension is _recalcReplayExtension()'s
        // job alone (single owner, Pillar 2; it runs right after via
        // _notifyRecordsChanged() and picks in _records list order, "first wins").
        if (record.contributions.providesReplayExtension) {
            QGCReplayExtension* ext = plugin->replayExtension();
            if (!ext) {
                qCWarning(QGCPluginManagerLog) << "Plugin" << pluginId
                    << "declares a replay extension in its manifest but provides none";
            } else if (_replayExtension && _replayExtension != ext) {
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
    _notifyRecordsChanged();
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
    PluginLoadInfo* record = _recordStore.find(pluginId);
    if (!record) {
        qCWarning(QGCPluginManagerLog) << "Plugin not found:" << pluginId;
        return;
    }

    // Persist the setting; QML sliders may already have written it, which is fine
    Fact* fact = _recordStore.pluginEnabledFact(pluginId);
    if (fact && fact->rawValue().toBool() != enabled) {
        fact->setRawValue(enabled);
    }

    if (enabled) {
        if (record->state == PluginState::Quarantined && record->manifest.id == _recordStore.crashedPluginId()) {
            // Re-enable is the one path out of crash quarantine: clear the marker and
            // route back through the trust gate — consent may still be required.
            qCDebug(QGCPluginManagerLog) << "Clearing crash quarantine for:" << pluginId;
            _recordStore.clearCrashQuarantine();
            record->state = PluginState::Discovered;
            record->errorString.clear();
            _applyTrustGate(*record);
            if (record->state == PluginState::Discovered) {
                _activateRecord(*record);
            }
            _notifyRecordsChanged();
        } else if (record->state == PluginState::Disabled || record->state == PluginState::Discovered) {
            qCDebug(QGCPluginManagerLog) << "Enabling plugin:" << pluginId;
            _activateRecord(*record);
            _notifyRecordsChanged();
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

    PluginLoadInfo* record = _recordStore.find(pluginId);
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
        _applyTrustGate(fresh);

        if (fresh.state == PluginState::Discovered) {
            _activateIfEnabled(fresh);
        } else if (fresh.state != PluginState::NeedsApproval) {
            qCWarning(QGCPluginManagerLog) << "Reload inspection failed:" << record->filePath << "-" << fresh.errorString;
        }
        *record = fresh;
    }

    _notifyRecordsChanged();
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
    PluginLoadInfo* existing = _recordStore.find(installResult.pluginId);
    if (existing) {
        if (existing->state == PluginState::Active) {
            _deactivateRecord(*existing);
        }
        _recordStore.removeIf([&installResult](const PluginLoadInfo& r) {
            return r.manifest.id == installResult.pluginId;
        });
    }

    const QString packageDir = QDir(PluginInstaller::userPluginsDir()).filePath(installResult.pluginId);
    _processInspected({QGCPluginLoader::inspectPackage(packageDir)});

    // Picking the file in the install dialog is the explicit consent D10 asks for
    // (same intent reasoning as D14), so route the fresh record through the one
    // approval flow rather than making the user re-approve on the same page.
    PluginLoadInfo* installed = _recordStore.find(installResult.pluginId);
    if (installed && installed->state == PluginState::NeedsApproval) {
        approvePlugin(installResult.pluginId);
        installed = _recordStore.find(installResult.pluginId);
        if (installed && installed->state == PluginState::NeedsApproval) {
            return tr("Installed, but could not be auto-approved: %1").arg(installed->errorString);
        }
    }

    return QString();
}

QString QGCPluginManager::removePlugin(const QString& pluginId)
{
    qCDebug(QGCPluginManagerLog) << "Removing plugin:" << pluginId;

    PluginLoadInfo* record = _recordStore.find(pluginId);

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
            _notifyRecordsChanged();
        }
        return removeResult.errorString;
    }

    _recordStore.removeIf([&pluginId](const PluginLoadInfo& r) {
        return r.manifest.id == pluginId;
    });

    // Removal revokes consent: a copy of the same content arriving later (by any
    // means) starts unapproved again rather than inheriting the old approval.
    _recordStore.setApprovedPluginDigest(pluginId, QString());

    _notifyRecordsChanged();

    return QString();
}

void QGCPluginManager::approvePlugin(const QString& pluginId)
{
    qCDebug(QGCPluginManagerLog) << "Approving plugin:" << pluginId;

    PluginLoadInfo* record = _recordStore.find(pluginId);
    if (!record || record->state != PluginState::NeedsApproval) {
        qCWarning(QGCPluginManagerLog) << "Plugin not awaiting approval:" << pluginId;
        return;
    }

#if defined(Q_OS_MACOS)
    const QString stripTarget = record->packageDir.isEmpty() ? record->filePath : record->packageDir;
    if (!PluginInstaller::stripQuarantine(stripTarget)) {
        qCWarning(QGCPluginManagerLog) << "Could not fully strip quarantine from" << pluginId << "- leaving unapproved";
        return;
    }
#endif

    // Consent is keyed to the plugin's content (D10): a digest we cannot compute is a
    // plugin we cannot vouch for on the next scan, so approval fails closed.
    const QString digest = consentDigest(*record);
    if (digest.isEmpty()) {
        qCWarning(QGCPluginManagerLog) << "Could not read" << pluginId << "to record consent - leaving unapproved";
        return;
    }
    _recordStore.setApprovedPluginDigest(pluginId, digest);

    record->state = PluginState::Discovered;
    record->errorString.clear();

    _activateIfEnabled(*record);

    _notifyRecordsChanged();
}
