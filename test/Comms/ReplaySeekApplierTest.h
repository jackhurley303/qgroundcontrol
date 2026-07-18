#pragma once

#include "VehicleTest.h"

class ReplaySeekApplierTest : public VehicleTest
{
    Q_OBJECT

private slots:
    void _missionUploadAppliesItems_test();
    void _seekMissionResolvedRestoresWithoutPlanFile_test();
    void _seekParamResolvedSetsValue_test();
    void _seekParamResolvedIgnoresOtherVehicle_test();
};
