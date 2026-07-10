/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QVersionNumber>

/// Identity/compatibility facts about the running host, used to validate a PluginManifest.
struct HostInfo
{
    QVersionNumber version;
    int apiVersion = 0;
    QString buildId;
};

/// Value type describing a plugin's identity, compatibility range, and contributions,
/// as declared in its qgcplugin.json manifest. Parsing and validation are pure data
/// operations: no plugin code runs to produce or check a PluginManifest.
class PluginManifest
{
public:
    enum class Tier
    {
        Qml,
        Sdk,
        Internal,
    };

    QString id;
    QString name;
    QVersionNumber version;
    QString vendor;
    QString description;
    Tier tier = Tier::Internal;
    int apiVersion = 0;
    QVersionNumber hostVersionMin;
    QVersionNumber hostVersionMax; ///< Null/empty QVersionNumber means unbounded.
    QString hostBuildId;
    QJsonObject contributes;

    /// Parses a manifest from its JSON object form. On failure returns a default-constructed
    /// PluginManifest and, if errorOut is non-null, a human-readable reason.
    static PluginManifest fromJson(const QJsonObject &json, QString *errorOut = nullptr);

    /// Checks this manifest's declared compatibility range against the running host.
    /// Returns true if the plugin may be activated; otherwise, if reasonOut is non-null,
    /// a human-readable reason is written and false is returned.
    bool validateForHost(const HostInfo &host, QString *reasonOut = nullptr) const;

    static QString tierToString(Tier tier);
    static bool tierFromString(const QString &str, Tier *tierOut);
};
