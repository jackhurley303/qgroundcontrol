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
#include <QtCore/QJsonValue>
#include <QtCore/QString>

/// JSON field helpers shared by the manifest and contributions parsers.
namespace PluginJson {

inline bool requireStringField(const QJsonObject &json, const char *key, QString *valueOut, QString *errorOut)
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

} // namespace PluginJson
