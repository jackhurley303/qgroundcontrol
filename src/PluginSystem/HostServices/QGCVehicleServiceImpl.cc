/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCVehicleServiceImpl.h"
#include "MultiVehicleManager.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"

QGCVehicleServiceImpl::QGCVehicleServiceImpl(QObject* parent)
    : QGCVehicleService(parent)
{
    MultiVehicleManager* mgr = MultiVehicleManager::instance();
    connect(mgr, &MultiVehicleManager::activeVehicleChanged, this, &QGCVehicleService::activeVehicleChanged);
    connect(mgr, &MultiVehicleManager::vehicleAdded, this, &QGCVehicleService::vehicleAdded);
    connect(mgr, &MultiVehicleManager::vehicleRemoved, this, &QGCVehicleService::vehicleRemoved);
}

QObject* QGCVehicleServiceImpl::activeVehicle() const
{
    return MultiVehicleManager::instance()->activeVehicle();
}

QList<QObject*> QGCVehicleServiceImpl::vehicles() const
{
    return *MultiVehicleManager::instance()->vehicles()->objectList();
}
