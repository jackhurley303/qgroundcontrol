/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCMissionService.h"

class Vehicle;

/**
 * @class QGCMissionServiceImpl
 * @brief Host implementation of "qgc.missions/1" over PlanMasterController.
 *
 * saveVehicleMissionToFile() uses a transient PlanMasterController: once a
 * vehicle's initial plan request has completed its mission is cached in the
 * vehicle's managers, so startStaticActiveVehicle() loads it synchronously
 * and no controller needs to outlive the call. missionReadyChanged() relays
 * each vehicle's initialPlanRequestCompleteChanged.
 */
class QGCMissionServiceImpl : public QGCMissionService
{
    Q_OBJECT

public:
    explicit QGCMissionServiceImpl(QObject* parent = nullptr);

    bool missionReady(int vehicleId) const override;
    bool saveVehicleMissionToFile(int vehicleId, const QString& filePath) override;

private:
    void _watchVehicle(Vehicle* vehicle);
};
