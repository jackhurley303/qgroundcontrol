/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginContributions.h"
#include "PluginJsonUtils.h"
#include "PluginManifest.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QPointF>

using PluginJson::requireStringField;

namespace {

bool optionalStringField(const QJsonObject &json, const char *key, QString *valueOut, QString *errorOut)
{
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isUndefined()) {
        valueOut->clear();
        return true;
    }
    if (!value.isString()) {
        if (errorOut) {
            *errorOut = QStringLiteral("field '%1' must be a string").arg(QString::fromLatin1(key));
        }
        return false;
    }
    *valueOut = value.toString();
    return true;
}

bool optionalNumberField(const QJsonObject &json, const char *key, double defaultValue, double *valueOut, QString *errorOut)
{
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isUndefined()) {
        *valueOut = defaultValue;
        return true;
    }
    if (!value.isDouble()) {
        if (errorOut) {
            *errorOut = QStringLiteral("field '%1' must be a number").arg(QString::fromLatin1(key));
        }
        return false;
    }
    *valueOut = value.toDouble();
    return true;
}

bool optionalBoolField(const QJsonObject &json, const char *key, bool *valueOut, QString *errorOut)
{
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isUndefined()) {
        *valueOut = false;
        return true;
    }
    if (!value.isBool()) {
        if (errorOut) {
            *errorOut = QStringLiteral("field '%1' must be a boolean").arg(QString::fromLatin1(key));
        }
        return false;
    }
    *valueOut = value.toBool();
    return true;
}

/// "defaultPosition": [x, y] as fractions of the parent size; (-1, -1) = framework default
bool optionalPositionField(const QJsonObject &json, const char *key, QPointF *valueOut, QString *errorOut)
{
    const QJsonValue value = json.value(QString::fromLatin1(key));
    if (value.isUndefined()) {
        *valueOut = QPointF(-1, -1);
        return true;
    }
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 2 || !array.at(0).isDouble() || !array.at(1).isDouble()) {
        if (errorOut) {
            *errorOut = QStringLiteral("field '%1' must be an array of two numbers").arg(QString::fromLatin1(key));
        }
        return false;
    }
    *valueOut = QPointF(array.at(0).toDouble(), array.at(1).toDouble());
    return true;
}

/// "qrc:/..." paths, bare "/..." paths, and any URL that already names a scheme
/// (contains "://" — http://, file://, ...) are compiled-in/host/already-absolute and
/// pass through verbatim; any other (relative) URL is package-relative. With no package
/// context (packageDir empty — the dev-loop bare-dylib path), a relative URL passes
/// through unresolved rather than guessing.
QString resolveUrl(const QString &url, const QString &packageDir)
{
    if (url.isEmpty() || url.startsWith(QStringLiteral("qrc:")) || url.startsWith(QLatin1Char('/'))
        || url.contains(QStringLiteral("://")) || packageDir.isEmpty()) {
        return url;
    }
    return QStringLiteral("file://%1/%2").arg(packageDir, url);
}

bool parseToolMenu(const QJsonObject &contributes, const PluginManifest &manifest, const QString &packageDir, QVariantMap *itemOut, QString *errorOut)
{
    const QJsonValue value = contributes.value(QStringLiteral("toolMenu"));
    if (value.isUndefined()) {
        return true;
    }
    if (!value.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("'toolMenu' must be an object");
        }
        return false;
    }
    const QJsonObject toolMenu = value.toObject();

    QString error;
    QString title;
    QString source;
    QString icon;
    QString toolbarSource;
    if (!requireStringField(toolMenu, "title", &title, &error)
        || !requireStringField(toolMenu, "source", &source, &error)
        || !optionalStringField(toolMenu, "icon", &icon, &error)
        || !optionalStringField(toolMenu, "toolbarSource", &toolbarSource, &error)) {
        if (errorOut) {
            *errorOut = QStringLiteral("toolMenu: %1").arg(error);
        }
        return false;
    }

    QVariantMap item;
    item[QStringLiteral("pluginId")]      = manifest.id;
    item[QStringLiteral("title")]         = title;
    item[QStringLiteral("icon")]          = resolveUrl(icon, packageDir);
    item[QStringLiteral("source")]        = resolveUrl(source, packageDir);
    item[QStringLiteral("toolbarSource")] = resolveUrl(toolbarSource, packageDir);
    *itemOut = item;
    return true;
}

bool parsePanel(const QJsonObject &contributes, const char *key, const PluginManifest &manifest, const QString &packageDir, QVariantMap *itemOut, QString *errorOut)
{
    const QJsonValue value = contributes.value(QString::fromLatin1(key));
    if (value.isUndefined()) {
        return true;
    }
    if (!value.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("'%1' must be an object").arg(QString::fromLatin1(key));
        }
        return false;
    }
    const QJsonObject panel = value.toObject();

    QString error;
    QString panelUrl;
    QString dockUrl;
    double defaultWidth = 0;
    double defaultHeight = 0;
    QPointF defaultPosition(-1, -1);
    if (!requireStringField(panel, "panel", &panelUrl, &error)
        || !optionalStringField(panel, "dock", &dockUrl, &error)
        || !optionalNumberField(panel, "defaultWidth", 0, &defaultWidth, &error)
        || !optionalNumberField(panel, "defaultHeight", 0, &defaultHeight, &error)
        || !optionalPositionField(panel, "defaultPosition", &defaultPosition, &error)) {
        if (errorOut) {
            *errorOut = QStringLiteral("%1: %2").arg(QString::fromLatin1(key), error);
        }
        return false;
    }

    QVariantMap item;
    item[QStringLiteral("pluginId")]         = manifest.id;
    item[QStringLiteral("name")]             = manifest.name;
    item[QStringLiteral("panelUrl")]         = resolveUrl(panelUrl, packageDir);
    item[QStringLiteral("dockUrl")]          = resolveUrl(dockUrl, packageDir);
    item[QStringLiteral("defaultWidth")]     = defaultWidth;
    item[QStringLiteral("defaultHeight")]    = defaultHeight;
    item[QStringLiteral("defaultXFraction")] = defaultPosition.x();
    item[QStringLiteral("defaultYFraction")] = defaultPosition.y();
    *itemOut = item;
    return true;
}

} // namespace

PluginContributions PluginContributions::fromManifest(const PluginManifest &manifest, const QString &packageDir, QString *errorOut)
{
    QString error;
    PluginContributions contributions;
    const QJsonObject contributes = manifest.contributes;

    if (!parseToolMenu(contributes, manifest, packageDir, &contributions.toolMenuItem, &error)
        || !parsePanel(contributes, "flyViewPanel", manifest, packageDir, &contributions.flyViewPanelItem, &error)
        || !parsePanel(contributes, "planViewPanel", manifest, packageDir, &contributions.planViewPanelItem, &error)
        || !optionalBoolField(contributes, "replay", &contributions.providesReplayExtension, &error)
        || !optionalBoolField(contributes, "telemetryLogging", &contributions.controlsTelemetryLogging, &error)) {
        if (errorOut) {
            *errorOut = QStringLiteral("invalid 'contributes': %1").arg(error);
        }
        return PluginContributions();
    }

    return contributions;
}
