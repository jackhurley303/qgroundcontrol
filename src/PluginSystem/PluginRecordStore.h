/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCPluginLoader.h"

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QScopeGuard>
#include <QtCore/QString>

#include <functional>

Q_DECLARE_LOGGING_CATEGORY(PluginRecordStoreLog)

class Fact;

/// @brief Owns the plugin record collection and all plugin persistence
///
/// Single owner (Pillar 2) of two things that were previously split across
/// QGCPluginManager: the QList<PluginLoadInfo> itself, and every persisted bit of
/// plugin state — the crash sentinel (raw QSettings, U3.4) and the PluginSettings
/// reach (registration, enabled-state, consent digests, D10). The crash sentinel
/// deliberately stays on raw QSettings rather than moving to the Fact system: its
/// write-then-sync-then-maybe-crash semantics are the point, and an async settings
/// layer inside a crash window would defeat them (Pillar 3).
class PluginRecordStore
{
public:
    PluginRecordStore() = default;

    /// The record collection, in discovery order. Mutable: callers mutate a record's
    /// state/plugin fields in place while iterating, the same access pattern the
    /// records had before this class existed.
    QList<PluginLoadInfo>& records() { return _records; }
    const QList<PluginLoadInfo>& records() const { return _records; }

    /// Find a record by manifest id, or nullptr if none is present.
    PluginLoadInfo* find(const QString& pluginId);

    /// Append a newly-inspected record to the collection.
    void append(PluginLoadInfo record) { _records.append(std::move(record)); }

    /// Drop every record for which predicate returns true.
    void removeIf(const std::function<bool(const PluginLoadInfo&)>& predicate);

    /// Drop every record. Crash-sentinel state (persisted and in-memory) is untouched
    /// — cleanup() is a runtime teardown, not a factory reset.
    void clear();

    /// Crash sentinel (U3.4): promote a lingering loadingPluginId from a previous run
    /// that never cleared it into the persistent crashedPluginId marker, then load
    /// crashedPluginId() into memory. Call once, before inspecting any plugin.
    void checkCrashSentinel();

    /// The manifest id blamed for crashing a previous run, or empty if none.
    QString crashedPluginId() const { return _crashedPluginId; }

    /// Clear the crash-quarantine marker, persisted and in-memory — the one path out
    /// of crash quarantine (an explicit re-enable).
    void clearCrashQuarantine();

    /// Arm the crash sentinel for one activation attempt: writes and syncs
    /// loadingPluginId before the caller's activation call, and returns a guard that
    /// clears and syncs it again on scope exit (success or failure alike). If the
    /// process dies between the two, the synced id lingers for the next
    /// checkCrashSentinel() to find. Ordering must not change — it is the
    /// crash-safety mechanism.
    [[nodiscard]] QScopeGuard<std::function<void()>> armLoadingSentinel(const QString& pluginId);

    /// Register a plugin and create its enabled Fact (PluginSettings::registerPlugin).
    void registerPlugin(const QString& pluginId, const QString& displayName, bool defaultEnabled);

    /// Whether a plugin is enabled (PluginSettings::isPluginEnabled).
    bool isPluginEnabled(const QString& pluginId) const;

    /// The enabled Fact for a plugin (PluginSettings::pluginEnabledFact).
    Fact* pluginEnabledFact(const QString& pluginId);

    /// The consent digest recorded when the user approved a plugin (D10), or empty if
    /// never approved (PluginSettings::approvedPluginDigest).
    QString approvedPluginDigest(const QString& pluginId) const;

    /// Record the consent digest for an approved plugin, or clear it on removal
    /// (PluginSettings::setApprovedPluginDigest).
    void setApprovedPluginDigest(const QString& pluginId, const QString& digest);

private:
    QList<PluginLoadInfo> _records;    // One record per discovered plugin, any state
    QString _crashedPluginId;          // Plugin blamed for crashing a previous run during its load
};
