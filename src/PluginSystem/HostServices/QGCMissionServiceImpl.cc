/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCMissionServiceImpl.h"
#include "AppSettings.h"
#include "MultiVehicleManager.h"
#include "PlanMasterController.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "Vehicle.h"

QGCMissionServiceImpl::QGCMissionServiceImpl(QObject* parent)
    : QGCMissionService(parent)
{
    MultiVehicleManager* mgr = MultiVehicleManager::instance();
    connect(mgr, &MultiVehicleManager::vehicleAdded, this, &QGCMissionServiceImpl::_watchVehicle);

    for (QObject* object : *mgr->vehicles()->objectList()) {
        _watchVehicle(qobject_cast<Vehicle*>(object));
    }
}

void QGCMissionServiceImpl::_watchVehicle(Vehicle* vehicle)
{
    if (!vehicle) {
        return;
    }

    // Connection drops with the vehicle, so no bookkeeping on removal
    connect(vehicle, &Vehicle::initialPlanRequestCompleteChanged, this,
            [this, vehicle](bool complete) {
                emit missionReadyChanged(vehicle->id(), complete);
            });
}

bool QGCMissionServiceImpl::missionReady(int vehicleId) const
{
    const Vehicle* vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    return vehicle && vehicle->initialPlanRequestComplete();
}

bool QGCMissionServiceImpl::saveVehicleMissionToFile(int vehicleId, const QString& filePath)
{
    Vehicle* vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    if (!vehicle || !vehicle->initialPlanRequestComplete()) {
        return false;
    }

    // startStaticActiveVehicle syncs the persisted offline-editing defaults to
    // this vehicle as a side effect; a snapshot must not mutate user settings,
    // so restore them (setRawValue is a no-op when the value is unchanged)
    AppSettings* appSettings = SettingsManager::instance()->appSettings();
    const QVariant savedFirmwareClass = appSettings->offlineEditingFirmwareClass()->rawValue();
    const QVariant savedVehicleClass = appSettings->offlineEditingVehicleClass()->rawValue();

    PlanMasterController controller;
    controller.startStaticActiveVehicle(vehicle);

    appSettings->offlineEditingFirmwareClass()->setRawValue(savedFirmwareClass);
    appSettings->offlineEditingVehicleClass()->setRawValue(savedVehicleClass);

    if (controller.syncInProgress()) {
        // The cached-items load didn't complete synchronously — a snapshot
        // now would be partial
        return false;
    }
    return controller.saveToFile(filePath);
}
