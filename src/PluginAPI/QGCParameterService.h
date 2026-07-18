/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

#include "qgc_plugin_api_global.h"

/// Versioned service id for QGCParameterService. Breaking changes ship as
/// "qgc.parameters/2", never as changes to this interface.
inline constexpr const char* QGCParameterServiceId = "qgc.parameters/1";

/**
 * @class QGCParameterService
 * @brief Host service for snapshotting a connected vehicle's parameters.
 *
 * Acquired via QGCHostServices::service(QGCParameterServiceId) and cast with
 * qobject_cast. Vehicle ids are the system ids exposed by QGCVehicleService
 * (the vehicle object's "id" property).
 *
 * A vehicle's parameters are *ready* once the host's initial parameter load
 * for it has completed — before that the parameter cache is incomplete and a
 * snapshot would be misleading, so saveVehicleParametersToFile() refuses.
 *
 * ABI: this vtable is frozen once the SDK ships — additions go to a
 * "qgc.parameters/2" interface, never here.
 */
class QGCPLUGINAPI_EXPORT QGCParameterService : public QObject
{
    Q_OBJECT

public:
    explicit QGCParameterService(QObject* parent = nullptr);
    ~QGCParameterService() override;

    /// Returns true once the host's initial parameter load for this vehicle
    /// has completed. False for unknown vehicle ids.
    virtual bool parametersReady(int vehicleId) const = 0;

    /// Write the vehicle's current parameters to filePath as a plain-text
    /// .params document (the same format the host's parameter-save UI
    /// produces). The file is written exactly to filePath — no extension is
    /// appended.
    /// @return false when the vehicle is unknown, its parameters aren't ready
    /// (parametersReady() false), or the write fails.
    virtual bool saveVehicleParametersToFile(int vehicleId, const QString& filePath) = 0;

signals:
    void parametersReadyChanged(int vehicleId, bool ready);
};
