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
    int qmlApiVersion = 0; ///< Current QGCPluginQmlApiLevel (see below)
};

/// The QML plugin API level: the `QGroundControl` QML singleton tree *is* the API for
/// tier qml packages (no binary, no C++ ABI — D1). Bumped when that QML-visible surface
/// changes incompatibly. A manifest's "qmlApiVersion", when declared, is checked against
/// this; undeclared is unchecked (mirrors apiVersion's qml-tier looseness, F9).
inline constexpr int QGCPluginQmlApiLevel = 1;

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
        HostPinned,
    };

    QString id;
    QString name;
    QVersionNumber version;
    QString vendor;
    QString description;
    Tier tier = Tier::HostPinned;
    int apiVersion = 0; ///< Required and exact-matched for sdk/internal; optional and unchecked for qml (no binary, no C++ ABI — D1).
    int qmlApiVersion = 0; ///< Tier qml only; 0 = undeclared, no check performed (see QGCPluginQmlApiLevel).
    QVersionNumber hostVersionMin;
    QVersionNumber hostVersionMax; ///< Null/empty QVersionNumber means unbounded.
    QString hostBuildId;
    QJsonObject contributes;

    /// Parses a manifest from its JSON object form. On failure returns a default-constructed
    /// PluginManifest and, if errorOut is non-null, a human-readable reason.
    static PluginManifest fromJson(const QJsonObject &json, QString *errorOut = nullptr);

    /// Parses a manifest from a QPluginLoader::metaData() envelope ({IID, className,
    /// MetaData, ...}) after checking the declared IID against expectedIid. On failure
    /// returns a default-constructed PluginManifest and a reason via errorOut.
    static PluginManifest fromMetaData(const QJsonObject &envelope, const QString &expectedIid, QString *errorOut = nullptr);

    /// Checks this manifest's declared compatibility range against the running host.
    /// Returns true if the plugin may be activated; otherwise, if reasonOut is non-null,
    /// a human-readable reason is written and false is returned.
    bool validateForHost(const HostInfo &host, QString *reasonOut = nullptr) const;

    static QString tierToString(Tier tier);
    static bool tierFromString(const QString &str, Tier *tierOut);
};
