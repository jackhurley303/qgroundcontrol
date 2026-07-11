/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>

#include "qgc_plugin_api_global.h"

/// Versioned service id for QGCVehicleService. Breaking changes ship as
/// "qgc.vehicles/2", never as changes to this interface.
inline constexpr const char* QGCVehicleServiceId = "qgc.vehicles/1";

/**
 * @class QGCVehicleService
 * @brief Host service exposing the connected vehicles.
 *
 * Acquired via QGCHostServices::service(QGCVehicleServiceId) and cast with
 * qobject_cast.
 *
 * The returned objects are the host's own Vehicle instances, handed over as
 * plain QObject*. Interact with them through the meta-object system —
 * property() / QMetaObject::invokeMethod / string-based connect — exactly the
 * surface the host's QML already consumes (e.g. property("id") is the
 * vehicle's system id). The host owns their lifetime: a vehicle is destroyed
 * after vehicleRemoved() is emitted for it, so hold vehicles via QPointer and
 * drop references on vehicleRemoved().
 *
 * ABI: this vtable is frozen once the SDK ships — additions go to a
 * "qgc.vehicles/2" interface, never here.
 */
class QGCPLUGINAPI_EXPORT QGCVehicleService : public QObject
{
    Q_OBJECT

public:
    explicit QGCVehicleService(QObject* parent = nullptr);
    ~QGCVehicleService() override;

    /// The active vehicle, or nullptr when no vehicle is connected.
    virtual QObject* activeVehicle() const = 0;

    /// Snapshot of all connected vehicles, in connection order.
    virtual QList<QObject*> vehicles() const = 0;

signals:
    /// @param vehicle The new active vehicle, or nullptr when none.
    void activeVehicleChanged(QObject* vehicle);
    void vehicleAdded(QObject* vehicle);
    /// The vehicle is still alive during emission and destroyed afterwards.
    void vehicleRemoved(QObject* vehicle);
};
