#pragma once

#include "VehicleTest.h"

class HostVehicleServicesTest : public VehicleTest
{
    Q_OBJECT

private slots:
    void _vehicleServiceExposesVehicle_test();
    void _vehicleServiceRelaysRemoval_test();
    void _missionServiceReadyAndSave_test();
    void _missionServiceRelaysReadyChanged_test();
    void _missionServiceUnknownVehicle_test();
    void _parameterServiceReadyAndSave_test();
    void _parameterServiceRelaysReadyChanged_test();
    void _parameterServiceUnknownVehicle_test();
};
