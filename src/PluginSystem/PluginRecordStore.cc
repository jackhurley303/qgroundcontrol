/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginRecordStore.h"
#include "Fact.h"
#include "PluginSettings.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"

#include <QtCore/QSettings>

QGC_LOGGING_CATEGORY(PluginRecordStoreLog, "PluginSystem.PluginRecordStore");

namespace {

// Crash sentinel (U3.4): loadingPluginId spans each activation attempt — a value
// still present at the next startup means the process died inside that plugin's
// load. It is then promoted to crashedPluginId, the persistent marker that keeps
// the plugin Quarantined until the user explicitly re-enables it (later
// activations of other plugins overwrite loadingPluginId, so the blame must not
// live there). Single-id by design: with two independently-crashing plugins the
// newest crash overwrites the older marker and the pair alternate across boots —
// anything better is a multi-id bisect, deliberately out of this scope.
constexpr const char* kLoadingPluginIdKey = "PluginSystem/loadingPluginId";
constexpr const char* kCrashedPluginIdKey = "PluginSystem/crashedPluginId";

PluginSettings* pluginSettings()
{
    return SettingsManager::instance()->pluginSettings();
}

} // namespace

PluginLoadInfo* PluginRecordStore::find(const QString& pluginId)
{
    for (PluginLoadInfo& record : _records) {
        if (record.manifest.id == pluginId) {
            return &record;
        }
    }
    return nullptr;
}

void PluginRecordStore::removeIf(const std::function<bool(const PluginLoadInfo&)>& predicate)
{
    _records.removeIf(predicate);
}

void PluginRecordStore::clear()
{
    _records.clear();
}

void PluginRecordStore::checkCrashSentinel()
{
    QSettings settings;
    const QString lingering = settings.value(QString::fromLatin1(kLoadingPluginIdKey)).toString();
    if (!lingering.isEmpty()) {
        qCWarning(PluginRecordStoreLog) << "Previous run crashed while loading plugin:" << lingering;
        settings.setValue(QString::fromLatin1(kCrashedPluginIdKey), lingering);
        settings.remove(QString::fromLatin1(kLoadingPluginIdKey));
        settings.sync();
    }
    _crashedPluginId = settings.value(QString::fromLatin1(kCrashedPluginIdKey)).toString();
}

void PluginRecordStore::clearCrashQuarantine()
{
    QSettings settings;
    settings.remove(QString::fromLatin1(kCrashedPluginIdKey));
    settings.sync();
    _crashedPluginId.clear();
}

QScopeGuard<std::function<void()>> PluginRecordStore::armLoadingSentinel(const QString& pluginId)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kLoadingPluginIdKey), pluginId);
    settings.sync();
    return qScopeGuard(std::function<void()>([] {
        QSettings settings;
        settings.remove(QString::fromLatin1(kLoadingPluginIdKey));
        settings.sync();
    }));
}

void PluginRecordStore::registerPlugin(const QString& pluginId, const QString& displayName, bool defaultEnabled)
{
    pluginSettings()->registerPlugin(pluginId, displayName, defaultEnabled);
}

bool PluginRecordStore::isPluginEnabled(const QString& pluginId) const
{
    return pluginSettings()->isPluginEnabled(pluginId);
}

Fact* PluginRecordStore::pluginEnabledFact(const QString& pluginId)
{
    return pluginSettings()->pluginEnabledFact(pluginId);
}

QString PluginRecordStore::approvedPluginDigest(const QString& pluginId) const
{
    return pluginSettings()->approvedPluginDigest(pluginId);
}

void PluginRecordStore::setApprovedPluginDigest(const QString& pluginId, const QString& digest)
{
    pluginSettings()->setApprovedPluginDigest(pluginId, digest);
}
