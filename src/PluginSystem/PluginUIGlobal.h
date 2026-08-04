/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtPositioning/QGeoCoordinate>
#include <QtQml/QJSEngine>
#include <QtQml/QJSValue>
#include <QtQml/QQmlEngine>
#include <QtQmlIntegration/QtQmlIntegration>

#include "QGroundControlQmlGlobal.h"

/// The published QML-visible facade over QGroundControlQmlGlobal, the app's god
/// object. Carries only the members that are contract (Part 3b of the plugin
/// architecture doc); the god object itself is not published, and neither is the
/// `import QGC` re-export.
///
/// Every member forwards to the one real QGroundControlQmlGlobal singleton
/// instance rather than holding a second copy of its state — see create() below
/// and PluginUIModule.cc for how that instance is located.
///
/// QML_NAMED_ELEMENT is used here (plain QML_ELEMENT would also work, since the
/// class name and QML name are the same) so it auto-registers under the
/// executable's root QGC module as "QGCPluginUIGlobal" — harmless, since
/// nothing imports it that way, but load-bearing: it is what gives this type an
/// `exports:` entry in the generated .qmltypes for tools/derive_plugin_ui_sdk.py
/// to carve. The actual publication under QGroundControl.PluginUI happens via
/// the imperative qmlRegisterSingletonType call in PluginUIModule.cc, which must
/// use this same class name — the derived SDK metadata is only reachable under
/// whatever name was carved from the auto-export.
class QGCPluginUIGlobal : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(QGCPluginUIGlobal)
    QML_SINGLETON

public:
    explicit QGCPluginUIGlobal(QGroundControlQmlGlobal *target, QObject *parent = nullptr);

    /// Locates the real QGroundControlQmlGlobal instance in the engine already
    /// running this QML and wraps it. Required by QML_SINGLETON in place of a
    /// default constructor; PluginUIModule.cc's registration under
    /// QGroundControl.PluginUI reuses this rather than duplicating the lookup.
    static QGCPluginUIGlobal *create(QQmlEngine *engine, QJSEngine *);

    Q_PROPERTY(QObject*        multiVehicleManager READ multiVehicleManager CONSTANT)
    Q_PROPERTY(QObject*        corePlugin           READ corePlugin          CONSTANT)
    Q_PROPERTY(QObject*        settingsManager      READ settingsManager     CONSTANT)
    Q_PROPERTY(QObject*        pluginManager        READ pluginManager       CONSTANT)
    Q_PROPERTY(QObject*        globalPalette        READ globalPalette       CONSTANT)
    Q_PROPERTY(qreal           zOrderTopMost        READ zOrderTopMost       CONSTANT)
    Q_PROPERTY(QGeoCoordinate  flightMapPosition    READ flightMapPosition   WRITE setFlightMapPosition  NOTIFY flightMapPositionChanged)
    Q_PROPERTY(double          flightMapZoom        READ flightMapZoom       WRITE setFlightMapZoom      NOTIFY flightMapZoomChanged)

    QObject *multiVehicleManager() const;
    QObject *corePlugin() const;
    QObject *settingsManager() const;
    QObject *pluginManager() const;
    QObject *globalPalette() const;
    qreal    zOrderTopMost() const;

    QGeoCoordinate flightMapPosition() const;
    void           setFlightMapPosition(const QGeoCoordinate &coordinate);
    double         flightMapZoom() const;
    void           setFlightMapZoom(double zoom);

    /// Forwards to QGroundControlQmlGlobal::showMessageDialog with the same signature.
    Q_INVOKABLE void showMessageDialog(
        QObject *owner,
        const QString &title,
        const QString &text,
        int buttons = QGroundControlQmlGlobal::kDefaultMessageDialogButtons,
        QJSValue acceptFunction = QJSValue(),
        QJSValue closeFunction = QJSValue());

    /// QGroundControlQmlGlobal::copyToClipboard is static and stateless; called
    /// directly rather than through the forwarded instance.
    Q_INVOKABLE static void copyToClipboard(const QString &text);

signals:
    void flightMapPositionChanged(QGeoCoordinate flightMapPosition);
    void flightMapZoomChanged(double flightMapZoom);

private:
    QGroundControlQmlGlobal *_target = nullptr;
};
