/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginCatalog.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QSet>

#include "PluginJsonUtils.h"

using PluginJson::requireStringField;

namespace {

// Largest integer a double holds exactly; keeps the qint64 cast below in range.
constexpr double MaxExactDouble = 9007199254740992.0;

const QString AnyPlatformKey = QStringLiteral("any");

// The loader's platform keys (platformBinarySubdir() in QGCPluginLoader.cc) plus `any`.
bool isKnownPackageKey(const QString& key)
{
    return key == AnyPlatformKey || key == QStringLiteral("macos-universal") || key == QStringLiteral("windows-x64") ||
           key == QStringLiteral("linux-x64");
}

// QVersionNumber::fromString() silently drops a suffix ("1.0.0-beta" parses as 1.0.0), which
// would make two different releases compare equal. The catalog accepts plain numeric versions only.
QVersionNumber parseStrictVersion(const QString& str)
{
    qsizetype suffixIndex = 0;
    const QVersionNumber version = QVersionNumber::fromString(str, &suffixIndex);
    if (version.isNull() || suffixIndex != str.size()) {
        return QVersionNumber();
    }
    return version;
}

bool isSha256Hex(const QString& str)
{
    if (str.size() != 64) {
        return false;
    }
    for (const QChar& ch : str) {
        // ASCII only: QChar::isDigit() is true for every Unicode decimal digit.
        const char16_t lower = ch.toLower().unicode();
        if (!(lower >= u'0' && lower <= u'9') && !(lower >= u'a' && lower <= u'f')) {
            return false;
        }
    }
    return true;
}

bool parsePackage(const QJsonValue& value, PluginCatalogPackage* packageOut, QString* errorOut)
{
    if (!value.isObject()) {
        *errorOut = QStringLiteral("must be an object");
        return false;
    }
    const QJsonObject obj = value.toObject();

    if (!requireStringField(obj, "url", &packageOut->url, errorOut) ||
        !requireStringField(obj, "sha256", &packageOut->sha256, errorOut)) {
        return false;
    }
    if (!isSha256Hex(packageOut->sha256)) {
        *errorOut = QStringLiteral("'sha256' must be 64 hexadecimal characters");
        return false;
    }
    packageOut->sha256 = packageOut->sha256.toLower();

    const QJsonValue sizeValue = obj.value(QStringLiteral("size"));
    const double size = sizeValue.toDouble();
    if (!sizeValue.isDouble() || size < 1 || size > MaxExactDouble || size != std::floor(size)) {
        *errorOut = QStringLiteral("missing or invalid required field 'size'");
        return false;
    }
    packageOut->size = static_cast<qint64>(size);
    return true;
}

bool parseVersion(const QJsonObject& json, const PluginCatalogEntry& entry, PluginCatalogVersion* versionOut,
                  QString* errorOut)
{
    QString error;

    QString versionStr;
    if (!requireStringField(json, "version", &versionStr, &error)) {
        *errorOut = error;
        return false;
    }
    if (parseStrictVersion(versionStr).isNull()) {
        *errorOut = QStringLiteral("invalid 'version' value '%1'").arg(versionStr);
        return false;
    }

    QString tierStr;
    if (!requireStringField(json, "tier", &tierStr, &error)) {
        *errorOut = error;
        return false;
    }
    PluginManifest::Tier tier = PluginManifest::Tier::Qml;
    if (!PluginManifest::tierFromString(tierStr, &tier) || tier == PluginManifest::Tier::HostPinned) {
        *errorOut = QStringLiteral("'tier' must be 'qml' or 'sdk', got '%1'").arg(tierStr);
        return false;
    }

    // The manifest parser owns the rest of the compatibility facts (apiVersion rules, the
    // qmlApiVersion floor, hostVersion.min required), so the catalog and an installed plugin
    // are held to the same definition.
    QJsonObject manifestJson;
    manifestJson[QStringLiteral("id")] = entry.id;
    manifestJson[QStringLiteral("name")] = entry.name;
    manifestJson[QStringLiteral("vendor")] = entry.author;
    manifestJson[QStringLiteral("description")] = entry.description;
    manifestJson[QStringLiteral("version")] = versionStr;
    manifestJson[QStringLiteral("tier")] = tierStr;
    manifestJson[QStringLiteral("apiVersion")] = json.value(QStringLiteral("apiVersion"));
    manifestJson[QStringLiteral("qmlApiVersion")] = json.value(QStringLiteral("qmlApiVersion"));
    manifestJson[QStringLiteral("hostVersion")] = json.value(QStringLiteral("hostVersion"));

    versionOut->manifest = PluginManifest::fromJson(manifestJson, &error);
    if (versionOut->manifest.id.isEmpty()) {
        *errorOut = error;
        return false;
    }

    const QJsonObject hostVersion = json.value(QStringLiteral("hostVersion")).toObject();
    for (const char* key : {"min", "max"}) {
        const QString text = hostVersion.value(QString::fromLatin1(key)).toString();
        if (!text.isEmpty() && parseStrictVersion(text).isNull()) {
            *errorOut = QStringLiteral("invalid 'hostVersion.%1' value '%2'").arg(QString::fromLatin1(key), text);
            return false;
        }
    }

    versionOut->released = json.value(QStringLiteral("released")).toString();
    versionOut->notes = json.value(QStringLiteral("notes")).toString();

    const QJsonValue packagesValue = json.value(QStringLiteral("packages"));
    if (!packagesValue.isObject()) {
        *errorOut = QStringLiteral("missing or non-object required field 'packages'");
        return false;
    }
    const QJsonObject packages = packagesValue.toObject();
    for (auto it = packages.begin(); it != packages.end(); ++it) {
        if (!isKnownPackageKey(it.key())) {
            continue;
        }
        if (it.key() == AnyPlatformKey && tier != PluginManifest::Tier::Qml) {
            *errorOut = QStringLiteral("package key 'any' is only valid for tier 'qml'");
            return false;
        }
        PluginCatalogPackage package;
        QString packageError;
        if (!parsePackage(it.value(), &package, &packageError)) {
            *errorOut = QStringLiteral("packages.%1: %2").arg(it.key(), packageError);
            return false;
        }
        versionOut->packages.insert(it.key(), package);
    }
    return true;
}

bool parseEntry(const QJsonObject& json, PluginCatalogEntry* entryOut, QString* errorOut)
{
    if (!requireStringField(json, "id", &entryOut->id, errorOut) ||
        !requireStringField(json, "name", &entryOut->name, errorOut) ||
        !requireStringField(json, "author", &entryOut->author, errorOut)) {
        return false;
    }

    entryOut->summary = json.value(QStringLiteral("summary")).toString();
    entryOut->description = json.value(QStringLiteral("description")).toString();
    entryOut->license = json.value(QStringLiteral("license")).toString();
    entryOut->homepage = json.value(QStringLiteral("homepage")).toString();
    entryOut->repository = json.value(QStringLiteral("repository")).toString();
    entryOut->icon = json.value(QStringLiteral("icon")).toString();
    for (const QJsonValue& screenshot : json.value(QStringLiteral("screenshots")).toArray()) {
        if (screenshot.isString()) {
            entryOut->screenshots.append(screenshot.toString());
        }
    }

    const QJsonValue versionsValue = json.value(QStringLiteral("versions"));
    if (!versionsValue.isArray()) {
        *errorOut = QStringLiteral("missing or non-array required field 'versions'");
        return false;
    }
    const QJsonArray versions = versionsValue.toArray();
    QSet<QVersionNumber> seen;
    for (qsizetype i = 0; i < versions.size(); ++i) {
        if (!versions.at(i).isObject()) {
            *errorOut = QStringLiteral("versions[%1] must be an object").arg(i);
            return false;
        }
        PluginCatalogVersion version;
        QString versionError;
        if (!parseVersion(versions.at(i).toObject(), *entryOut, &version, &versionError)) {
            *errorOut = QStringLiteral("versions[%1]: %2").arg(i).arg(versionError);
            return false;
        }
        if (seen.contains(version.manifest.version)) {
            *errorOut =
                QStringLiteral("versions[%1]: duplicate version '%2'").arg(i).arg(version.manifest.version.toString());
            return false;
        }
        seen.insert(version.manifest.version);
        entryOut->versions.append(version);
    }
    return true;
}

}  // namespace

std::optional<PluginCatalogOffer> PluginCatalogEntry::newestCompatible(const HostInfo& host,
                                                                       const QString& platformKey) const
{
    std::optional<PluginCatalogOffer> best = std::nullopt;
    for (const PluginCatalogVersion& version : versions) {
        if (best && version.manifest.version <= best->version.manifest.version) {
            continue;
        }
        if (!version.manifest.validateForHost(host)) {
            continue;
        }
        auto package = version.packages.constFind(platformKey);
        if (package == version.packages.constEnd()) {
            package = version.packages.constFind(AnyPlatformKey);
        }
        if (package == version.packages.constEnd()) {
            continue;
        }
        best = PluginCatalogOffer{version, package.value()};
    }
    return best;
}

std::optional<PluginCatalogOffer> PluginCatalogEntry::updateFor(const HostInfo& host, const QString& platformKey,
                                                                const QVersionNumber& installed) const
{
    const std::optional<PluginCatalogOffer> offer = newestCompatible(host, platformKey);
    if (offer && QVersionNumber::compare(offer->version.manifest.version, installed) > 0) {
        return offer;
    }
    return std::nullopt;
}

PluginCatalog PluginCatalog::fromJson(const QJsonObject& json, QString* errorOut)
{
    auto fail = [errorOut](const QString& message) {
        if (errorOut) {
            *errorOut = message;
        }
        return PluginCatalog();
    };

    const QJsonValue schemaValue = json.value(QStringLiteral("schemaVersion"));
    const double schema = schemaValue.toDouble();
    if (!schemaValue.isDouble() || schema < 1 || schema != std::floor(schema)) {
        return fail(QStringLiteral("missing or invalid required field 'schemaVersion'"));
    }
    if (schema > SupportedSchemaVersion) {
        PluginCatalog newer;
        newer.schemaVersion = static_cast<int>(std::min(schema, static_cast<double>(std::numeric_limits<int>::max())));
        if (errorOut) {
            *errorOut = QStringLiteral("catalog schemaVersion %1 is newer than the supported %2")
                            .arg(newer.schemaVersion)
                            .arg(SupportedSchemaVersion);
        }
        return newer;
    }

    PluginCatalog catalog;
    catalog.schemaVersion = static_cast<int>(schema);
    catalog.generated = json.value(QStringLiteral("generated")).toString();

    const QJsonValue pluginsValue = json.value(QStringLiteral("plugins"));
    if (!pluginsValue.isArray()) {
        return fail(QStringLiteral("missing or non-array required field 'plugins'"));
    }
    const QJsonArray plugins = pluginsValue.toArray();
    QSet<QString> seenIds;
    for (qsizetype i = 0; i < plugins.size(); ++i) {
        if (!plugins.at(i).isObject()) {
            return fail(QStringLiteral("plugins[%1] must be an object").arg(i));
        }
        PluginCatalogEntry entry;
        QString entryError;
        if (!parseEntry(plugins.at(i).toObject(), &entry, &entryError)) {
            return fail(QStringLiteral("plugins[%1]: %2").arg(i).arg(entryError));
        }
        if (seenIds.contains(entry.id)) {
            return fail(QStringLiteral("plugins[%1]: duplicate plugin id '%2'").arg(i).arg(entry.id));
        }
        seenIds.insert(entry.id);
        catalog.plugins.append(entry);
    }
    return catalog;
}

PluginCatalog PluginCatalog::fromData(const QByteArray& data, QString* errorOut)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) {
            *errorOut = parseError.error != QJsonParseError::NoError
                            ? QStringLiteral("index is not valid JSON: %1").arg(parseError.errorString())
                            : QStringLiteral("index is not a JSON object");
        }
        return PluginCatalog();
    }
    return fromJson(doc.object(), errorOut);
}

const PluginCatalogEntry* PluginCatalog::find(const QString& id) const
{
    for (const PluginCatalogEntry& entry : plugins) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}
