/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCParameterService.h"

class Vehicle;

/**
 * @class QGCParameterServiceImpl
 * @brief Host implementation of "qgc.parameters/1" over ParameterManager.
 *
 * saveVehicleParametersToFile() streams ParameterManager's synchronous
 * writeParametersToStream() into the target file — once parametersReady()
 * is true the cached parameter set is complete, so no state needs to
 * outlive the call. parametersReadyChanged() relays each vehicle's
 * ParameterManager::parametersReadyChanged.
 */
class QGCParameterServiceImpl : public QGCParameterService
{
    Q_OBJECT

public:
    explicit QGCParameterServiceImpl(QObject* parent = nullptr);

    bool parametersReady(int vehicleId) const override;
    bool saveVehicleParametersToFile(int vehicleId, const QString& filePath) override;

private:
    void _watchVehicle(Vehicle* vehicle);
};
