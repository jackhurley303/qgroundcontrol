/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <optional>

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVersionNumber>

#include "PluginManifest.h"

/// One downloadable file of a catalog version, for one platform key.
struct PluginCatalogPackage
{
    QString url;
    QString sha256;  ///< Lowercase hex, 64 characters.
    qint64 size = 0;
};

/// One published version of a catalog plugin.
struct PluginCatalogVersion
{
    /// The version's compatibility facts in manifest form (the index's `author` fills `vendor`),
    /// so the catalog applies the same check an installed plugin passes.
    PluginManifest manifest;
    QString released;
    QString notes;
    /// Keyed by platform key (`macos-universal`, `windows-x64`, `linux-x64`) or `any` (qml tier
    /// only). Keys this build does not know are dropped at parse time.
    QHash<QString, PluginCatalogPackage> packages;
};

/// The version and package a host would install.
struct PluginCatalogOffer
{
    PluginCatalogVersion version;
    PluginCatalogPackage package;
};

/// One plugin listed in the index.
struct PluginCatalogEntry
{
    QString id;
    QString name;
    QString author;
    QString summary;
    QString description;
    QString license;
    QString homepage;
    QString repository;
    QString icon;
    QStringList screenshots;
    QList<PluginCatalogVersion> versions;

    /// The newest version that passes PluginManifest::validateForHost() and has a package for
    /// platformKey, or an `any` package. A version with no such package is skipped silently.
    std::optional<PluginCatalogOffer> newestCompatible(const HostInfo& host, const QString& platformKey) const;

    /// Like newestCompatible(), but only when that version is newer than `installed`.
    std::optional<PluginCatalogOffer> updateFor(const HostInfo& host, const QString& platformKey,
                                                const QVersionNumber& installed) const;
};

/// Value type for the plugin catalog's index.json (schema v1). Parsing is a pure data
/// operation. Unknown fields and unknown package keys are ignored, so a later schema can add
/// them without breaking this build.
class PluginCatalog
{
public:
    static constexpr int SupportedSchemaVersion = 1;

    int schemaVersion = 0;
    QString generated;
    QList<PluginCatalogEntry> plugins;

    /// Parses an index from its JSON object form. On failure returns a catalog with no plugins
    /// and, if errorOut is non-null, a human-readable reason. A `schemaVersion` above
    /// SupportedSchemaVersion is a failure that keeps the declared number, so needsNewerHost()
    /// is true on the result.
    static PluginCatalog fromJson(const QJsonObject& json, QString* errorOut = nullptr);

    /// Parses index.json bytes. Same failure contract as fromJson().
    static PluginCatalog fromData(const QByteArray& data, QString* errorOut = nullptr);

    /// True when the index declared a schema newer than this build understands.
    bool needsNewerHost() const { return schemaVersion > SupportedSchemaVersion; }

    const PluginCatalogEntry* find(const QString& id) const;
};
