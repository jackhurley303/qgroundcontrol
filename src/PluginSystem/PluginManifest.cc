/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginManifest.h"

namespace {

bool requireStringField(const QJsonObject &json, const char *key, QString *valueOut, QString *errorOut)
{
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (!value.isString() || value.toString().isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("missing or empty required field '%1'").arg(QString::fromLatin1(key));
        }
        return false;
    }
    *valueOut = value.toString();
    return true;
}

} // namespace

QString PluginManifest::tierToString(Tier tier)
{
    switch (tier) {
    case Tier::Qml:
        return QStringLiteral("qml");
    case Tier::Sdk:
        return QStringLiteral("sdk");
    case Tier::Internal:
        return QStringLiteral("internal");
    }
    return QString();
}

bool PluginManifest::tierFromString(const QString &str, Tier *tierOut)
{
    if (str == QStringLiteral("qml")) {
        *tierOut = Tier::Qml;
        return true;
    }
    if (str == QStringLiteral("sdk")) {
        *tierOut = Tier::Sdk;
        return true;
    }
    if (str == QStringLiteral("internal")) {
        *tierOut = Tier::Internal;
        return true;
    }
    return false;
}

PluginManifest PluginManifest::fromJson(const QJsonObject &json, QString *errorOut)
{
    QString error;
    PluginManifest manifest;

    if (!requireStringField(json, "id", &manifest.id, &error)
        || !requireStringField(json, "name", &manifest.name, &error)
        || !requireStringField(json, "vendor", &manifest.vendor, &error)) {
        if (errorOut) {
            *errorOut = error;
        }
        return PluginManifest();
    }

    manifest.description = json.value(QStringLiteral("description")).toString();

    QString versionStr;
    if (!requireStringField(json, "version", &versionStr, &error)) {
        if (errorOut) {
            *errorOut = error;
        }
        return PluginManifest();
    }
    manifest.version = QVersionNumber::fromString(versionStr);
    if (manifest.version.isNull()) {
        if (errorOut) {
            *errorOut = QStringLiteral("invalid 'version' value '%1'").arg(versionStr);
        }
        return PluginManifest();
    }

    QString tierStr;
    if (!requireStringField(json, "tier", &tierStr, &error)) {
        if (errorOut) {
            *errorOut = error;
        }
        return PluginManifest();
    }
    if (!tierFromString(tierStr, &manifest.tier)) {
        if (errorOut) {
            *errorOut = QStringLiteral("unknown 'tier' value '%1'").arg(tierStr);
        }
        return PluginManifest();
    }

    const QJsonValue apiVersionValue = json.value(QStringLiteral("apiVersion"));
    if (!apiVersionValue.isDouble()) {
        if (errorOut) {
            *errorOut = QStringLiteral("missing or non-numeric required field 'apiVersion'");
        }
        return PluginManifest();
    }
    manifest.apiVersion = apiVersionValue.toInt();

    const QJsonValue hostVersionValue = json.value(QStringLiteral("hostVersion"));
    if (!hostVersionValue.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("missing or non-object required field 'hostVersion'");
        }
        return PluginManifest();
    }
    const QJsonObject hostVersionObj = hostVersionValue.toObject();

    QString minStr;
    if (!requireStringField(hostVersionObj, "min", &minStr, &error)) {
        if (errorOut) {
            *errorOut = QStringLiteral("hostVersion.min: %1").arg(error);
        }
        return PluginManifest();
    }
    manifest.hostVersionMin = QVersionNumber::fromString(minStr);
    if (manifest.hostVersionMin.isNull()) {
        if (errorOut) {
            *errorOut = QStringLiteral("invalid 'hostVersion.min' value '%1'").arg(minStr);
        }
        return PluginManifest();
    }

    const QString maxStr = hostVersionObj.value(QStringLiteral("max")).toString();
    if (!maxStr.isEmpty()) {
        manifest.hostVersionMax = QVersionNumber::fromString(maxStr);
        if (manifest.hostVersionMax.isNull()) {
            if (errorOut) {
                *errorOut = QStringLiteral("invalid 'hostVersion.max' value '%1'").arg(maxStr);
            }
            return PluginManifest();
        }
    }

    manifest.hostBuildId = json.value(QStringLiteral("hostBuildId")).toString();
    if (manifest.tier == Tier::Internal && manifest.hostBuildId.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("'hostBuildId' is required when tier is 'internal'");
        }
        return PluginManifest();
    }

    manifest.contributes = json.value(QStringLiteral("contributes")).toObject();

    return manifest;
}

PluginManifest PluginManifest::fromMetaData(const QJsonObject &envelope, const QString &expectedIid, QString *errorOut)
{
    const QString iid = envelope.value(QStringLiteral("IID")).toString();
    if (iid != expectedIid) {
        if (errorOut) {
            *errorOut = QStringLiteral("plugin IID '%1' does not match expected IID '%2'").arg(iid, expectedIid);
        }
        return PluginManifest();
    }

    // Qt may emit the MetaData key as an empty object when Q_PLUGIN_METADATA has no
    // FILE argument, so treat empty the same as absent to keep the diagnostic accurate.
    const QJsonValue metaDataValue = envelope.value(QStringLiteral("MetaData"));
    if (!metaDataValue.isObject() || metaDataValue.toObject().isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("no embedded qgcplugin.json manifest (Q_PLUGIN_METADATA missing FILE argument?)");
        }
        return PluginManifest();
    }

    return fromJson(metaDataValue.toObject(), errorOut);
}

bool PluginManifest::validateForHost(const HostInfo &host, QString *reasonOut) const
{
    if (apiVersion != host.apiVersion) {
        if (reasonOut) {
            *reasonOut = QStringLiteral("apiVersion %1 does not match host apiVersion %2").arg(apiVersion).arg(host.apiVersion);
        }
        return false;
    }

    // A host built from a tagless checkout has no parseable version (git describe yields a
    // bare hash), making the range unenforceable — validate what we can rather than reject all.
    if (!host.version.isNull()) {
        if (host.version < hostVersionMin) {
            if (reasonOut) {
                *reasonOut = QStringLiteral("requires QGC >= %1, host is %2").arg(hostVersionMin.toString(), host.version.toString());
            }
            return false;
        }

        if (!hostVersionMax.isNull() && host.version >= hostVersionMax) {
            if (reasonOut) {
                *reasonOut = QStringLiteral("requires QGC < %1, host is %2").arg(hostVersionMax.toString(), host.version.toString());
            }
            return false;
        }
    }

    if (tier == Tier::Internal && hostBuildId != host.buildId) {
        if (reasonOut) {
            *reasonOut = QStringLiteral("built for another QGC build (%1), host build is %2").arg(hostBuildId, host.buildId);
        }
        return false;
    }

    return true;
}
