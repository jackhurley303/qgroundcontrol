/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCParameterServiceImpl.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>

#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"

QGCParameterServiceImpl::QGCParameterServiceImpl(QObject* parent)
    : QGCParameterService(parent)
{
    MultiVehicleManager* mgr = MultiVehicleManager::instance();
    connect(mgr, &MultiVehicleManager::vehicleAdded, this, &QGCParameterServiceImpl::_watchVehicle);

    for (QObject* object : *mgr->vehicles()->objectList()) {
        _watchVehicle(qobject_cast<Vehicle*>(object));
    }
}

void QGCParameterServiceImpl::_watchVehicle(Vehicle* vehicle)
{
    if (!vehicle) {
        return;
    }

    // Connection drops with the vehicle, so no bookkeeping on removal
    connect(vehicle->parameterManager(), &ParameterManager::parametersReadyChanged, this,
            [this, vehicle](bool ready) {
                emit parametersReadyChanged(vehicle->id(), ready);
            });
}

bool QGCParameterServiceImpl::parametersReady(int vehicleId) const
{
    const Vehicle* vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    return vehicle && vehicle->parameterManager()->parametersReady();
}

bool QGCParameterServiceImpl::saveVehicleParametersToFile(int vehicleId, const QString& filePath)
{
    Vehicle* vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    if (!vehicle || !vehicle->parameterManager()->parametersReady()) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QTextStream stream(&file);
    vehicle->parameterManager()->writeParametersToStream(stream);

    // writeParametersToStream flushes, so a device write failure (disk full,
    // I/O error) is visible here — the contract promises false for it
    return (stream.status() == QTextStream::Ok) && (file.error() == QFileDevice::NoError);
}
