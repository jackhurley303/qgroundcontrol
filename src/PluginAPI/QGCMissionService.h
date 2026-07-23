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

/// Versioned service id for QGCMissionService. Breaking changes ship as
/// "qgc.missions/2", never as changes to this interface.
inline constexpr const char* QGCMissionServiceId = "qgc.missions/1";

/**
 * @class QGCMissionService
 * @ingroup PluginAPI
 * @brief Host service for snapshotting a connected vehicle's mission.
 *
 * Acquired via QGCHostServices::service(QGCMissionServiceId) and cast with
 * qobject_cast. Vehicle ids are the system ids exposed by QGCVehicleService
 * (the vehicle object's "id" property).
 *
 * A vehicle's mission is *ready* once the host's initial plan request for it
 * has completed — before that the mission cache is incomplete and a snapshot
 * would be misleading, so saveVehicleMissionToFile() refuses.
 *
 * ABI: this vtable is frozen once the SDK ships — additions go to a
 * "qgc.missions/2" interface, never here.
 */
class QGCPLUGINAPI_EXPORT QGCMissionService : public QObject
{
    Q_OBJECT

public:
    explicit QGCMissionService(QObject* parent = nullptr);
    ~QGCMissionService() override;

    /// Returns true once the host's initial plan request for this vehicle has
    /// completed. False for unknown vehicle ids.
    virtual bool missionReady(int vehicleId) const = 0;

    /// Write the vehicle's current mission (mission items, geofence, rally
    /// points) to filePath as a .plan document. If filePath's filename has no
    /// extension, ".plan" is appended — pass a path ending in ".plan" to get
    /// the file exactly where named. For a vehicle id with a registered
    /// replay plan file (see QGCReplayService), the snapshot reflects that
    /// registered plan.
    /// @return false when the vehicle is unknown, its mission isn't ready
    /// (missionReady() false), or the write fails.
    virtual bool saveVehicleMissionToFile(int vehicleId, const QString& filePath) = 0;

signals:
    void missionReadyChanged(int vehicleId, bool ready);
};
