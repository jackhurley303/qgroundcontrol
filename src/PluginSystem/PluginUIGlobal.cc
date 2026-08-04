/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginUIGlobal.h"

#include "MultiVehicleManager.h"
#include "QGCCorePlugin.h"
#include "QGCPluginManager.h"
#include "SettingsManager.h"

QGCPluginUIGlobal::QGCPluginUIGlobal(QGroundControlQmlGlobal *target, QObject *parent)
    : QObject(parent)
    , _target(target)
{
    connect(_target, &QGroundControlQmlGlobal::flightMapPositionChanged, this, &QGCPluginUIGlobal::flightMapPositionChanged);
    connect(_target, &QGroundControlQmlGlobal::flightMapZoomChanged, this, &QGCPluginUIGlobal::flightMapZoomChanged);
}

QGCPluginUIGlobal *QGCPluginUIGlobal::create(QQmlEngine *engine, QJSEngine *)
{
    auto *target = engine->singletonInstance<QGroundControlQmlGlobal*>(QStringLiteral("QGC"), QStringLiteral("QGroundControl"));
    return new QGCPluginUIGlobal(target);
}

QObject *QGCPluginUIGlobal::multiVehicleManager() const { return _target->multiVehicleManager(); }
QObject *QGCPluginUIGlobal::corePlugin() const { return _target->corePlugin(); }
QObject *QGCPluginUIGlobal::settingsManager() const { return _target->settingsManager(); }
QObject *QGCPluginUIGlobal::pluginManager() const { return _target->pluginManager(); }
QObject *QGCPluginUIGlobal::globalPalette() const { return _target->property("globalPalette").value<QObject*>(); }
qreal    QGCPluginUIGlobal::zOrderTopMost() const { return _target->zOrderTopMost(); }

QGeoCoordinate QGCPluginUIGlobal::flightMapPosition() const { return QGroundControlQmlGlobal::flightMapPosition(); }
double         QGCPluginUIGlobal::flightMapZoom() const { return QGroundControlQmlGlobal::flightMapZoom(); }

void QGCPluginUIGlobal::setFlightMapPosition(const QGeoCoordinate &coordinate)
{
    QGeoCoordinate mutableCoordinate = coordinate;
    _target->setFlightMapPosition(mutableCoordinate);
}

void QGCPluginUIGlobal::setFlightMapZoom(double zoom)
{
    _target->setFlightMapZoom(zoom);
}

void QGCPluginUIGlobal::showMessageDialog(
    QObject *owner,
    const QString &title,
    const QString &text,
    int buttons,
    QJSValue acceptFunction,
    QJSValue closeFunction)
{
    _target->showMessageDialog(owner, title, text, buttons, acceptFunction, closeFunction);
}

void QGCPluginUIGlobal::copyToClipboard(const QString &text)
{
    QGroundControlQmlGlobal::copyToClipboard(text);
}
