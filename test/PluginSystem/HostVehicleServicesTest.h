#pragma once

#include <QtCore/QString>

#include "VehicleTest.h"

class Fact;

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
    void _parameterDiffRoundTrip_test();
    void _parameterDiffUnchangedFileHasNoEntries_test();
    void _parameterDiffOffVehicleParameter_test();
    void _parameterDiffRefusesReadOnlyWrite_test();
    void _parameterDiffMissionPlannerFormat_test();
    void _parameterDiffRejectsUnusableValues_test();
    void _parameterDiffReportsFileFlags_test();
    void _parameterDiffRejectsBadInput_test();

private:
    /// A writable, non-enumerated numeric parameter of the connected vehicle —
    /// picked at runtime so the tests don't rot with MockLink's parameter set.
    Fact* _diffableParameter();
    /// Writes a parameter file holding exactly the given lines.
    bool _writeParamFile(const QString& filePath, const QStringList& lines);
    /// One QGC-format line: VehicleId ComponentId Name Value Type.
    QString _qgcParamLine(int vehicleId, int componentId, const QString& name, const QString& value, int mavType);
};
