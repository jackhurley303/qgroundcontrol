/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginManifest.h"
#include "PluginJsonUtils.h"

using PluginJson::requireStringField;

QString PluginManifest::tierToString(Tier tier)
{
    switch (tier) {
    case Tier::Qml:
        return QStringLiteral("qml");
    case Tier::Sdk:
        return QStringLiteral("sdk");
    case Tier::HostPinned:
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
        *tierOut = Tier::HostPinned;
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

    // apiVersion gates the C++ ABI. Tier qml packages (Stage 3) carry no binary and never
    // touch it (D1) — required and exact-matched for sdk/internal, optional and unchecked
    // for qml (see validateForHost; qmlApiVersion is the qml compatibility axis, U3.1).
    const QJsonValue apiVersionValue = json.value(QStringLiteral("apiVersion"));
    if (manifest.tier == Tier::Qml) {
        manifest.apiVersion = apiVersionValue.isDouble() ? apiVersionValue.toInt() : 0;
    } else {
        if (!apiVersionValue.isDouble()) {
            if (errorOut) {
                *errorOut = QStringLiteral("missing or non-numeric required field 'apiVersion'");
            }
            return PluginManifest();
        }
        manifest.apiVersion = apiVersionValue.toInt();
    }

    // qmlApiVersion is the qml-tier compatibility axis (U3.1): meaningful, and therefore
    // parsed/validated, only for tier qml — ignored (like an unknown key) on every other
    // tier, so an irrelevant or stray field there never breaks an otherwise-valid manifest.
    const QJsonValue qmlApiVersionValue = json.value(QStringLiteral("qmlApiVersion"));
    if (manifest.tier == Tier::Qml && !qmlApiVersionValue.isUndefined()) {
        if (!qmlApiVersionValue.isDouble() || qmlApiVersionValue.toInt() < 1) {
            if (errorOut) {
                *errorOut = QStringLiteral("field 'qmlApiVersion' must be a number >= 1");
            }
            return PluginManifest();
        }
        manifest.qmlApiVersion = qmlApiVersionValue.toInt();
    }

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
    if (manifest.tier == Tier::HostPinned && manifest.hostBuildId.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("'hostBuildId' is required when tier is 'internal'");
        }
        return PluginManifest();
    }

    const QJsonValue contributesValue = json.value(QStringLiteral("contributes"));
    if (!contributesValue.isUndefined() && !contributesValue.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("'contributes' must be an object");
        }
        return PluginManifest();
    }
    manifest.contributes = contributesValue.toObject();

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
    if (tier != Tier::Qml && apiVersion != host.apiVersion) {
        if (reasonOut) {
            *reasonOut = QStringLiteral("apiVersion %1 does not match host apiVersion %2").arg(apiVersion).arg(host.apiVersion);
        }
        return false;
    }

    // Qt keeps binary compatibility across patch releases and within a major for older minors, so
    // only a different major or a newer minor would fail at load ("uses incompatible Qt library").
    if (tier != Tier::Qml && !qtVersion.isNull() && !host.qtVersion.isNull() &&
        (qtVersion.majorVersion() != host.qtVersion.majorVersion() ||
         qtVersion.minorVersion() > host.qtVersion.minorVersion())) {
        if (reasonOut) {
            *reasonOut = QStringLiteral("built with Qt %1.%2; this QGroundControl runs Qt %3.%4")
                             .arg(qtVersion.majorVersion())
                             .arg(qtVersion.minorVersion())
                             .arg(host.qtVersion.majorVersion())
                             .arg(host.qtVersion.minorVersion());
        }
        return false;
    }

#if defined(Q_OS_WIN)
    constexpr bool hostIsWindows = true;
#else
    constexpr bool hostIsWindows = false;
#endif
    if (tier != Tier::Qml && debugBuild.has_value() &&
        !debugBuildCompatible(host.debugBuild, *debugBuild, hostIsWindows)) {
        if (reasonOut) {
            *reasonOut = *debugBuild ? QStringLiteral("a Debug build cannot load in this Release QGroundControl")
                                     : QStringLiteral("a Release build cannot load in this Debug QGroundControl");
        }
        return false;
    }

    if (tier == Tier::Qml && qmlApiVersion != 0 && qmlApiVersion != host.qmlApiVersion) {
        if (reasonOut) {
            *reasonOut = QStringLiteral("qmlApiVersion %1 does not match host qmlApiVersion %2").arg(qmlApiVersion).arg(host.qmlApiVersion);
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

    if (tier == Tier::HostPinned && hostBuildId != host.buildId) {
        if (reasonOut) {
            *reasonOut = QStringLiteral("built for another QGC build (%1), host build is %2").arg(hostBuildId, host.buildId);
        }
        return false;
    }

    return true;
}

void PluginManifest::readQtBuildFacts(const QJsonObject& envelope)
{
    if (tier == Tier::Qml) {
        return;
    }

    // Qt packs the metadata header's major and minor as (major << 16) | (minor << 8); the patch byte is 0.
    const QJsonValue versionValue = envelope.value(QStringLiteral("version"));
    if (versionValue.isDouble()) {
        const int packed = versionValue.toInt();
        qtVersion = QVersionNumber((packed >> 16) & 0xff, (packed >> 8) & 0xff);
    }

    const QJsonValue debugValue = envelope.value(QStringLiteral("debug"));
    if (debugValue.isBool()) {
        debugBuild = debugValue.toBool();
    }
}

bool PluginManifest::debugBuildCompatible(bool hostDebug, bool pluginDebug, bool hostIsWindows)
{
    return !hostIsWindows || hostDebug == pluginDebug;
}
