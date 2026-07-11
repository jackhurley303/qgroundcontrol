#include "HostVehicleServicesTest.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>

#include "HostServices/QGCMissionServiceImpl.h"
#include "HostServices/QGCVehicleServiceImpl.h"
#include "Vehicle.h"

void HostVehicleServicesTest::_vehicleServiceExposesVehicle_test()
{
    QGCVehicleServiceImpl service;

    QCOMPARE(service.activeVehicle(), vehicle());
    QVERIFY(service.vehicles().contains(vehicle()));

    // Plugins consume the handed-over vehicle through the meta-object surface
    QCOMPARE(service.activeVehicle()->property("id").toInt(), vehicle()->id());
}

void HostVehicleServicesTest::_vehicleServiceRelaysRemoval_test()
{
    QGCVehicleServiceImpl service;
    QObject* connected = vehicle();

    QSignalSpy removedSpy(&service, &QGCVehicleService::vehicleRemoved);
    QSignalSpy activeSpy(&service, &QGCVehicleService::activeVehicleChanged);

    _disconnectMockLink();

    QVERIFY(removedSpy.count() >= 1);
    // Pointer identity only — the vehicle is destroyed after removal
    QCOMPARE(removedSpy.last().at(0).value<QObject*>(), connected);
    QVERIFY(activeSpy.count() >= 1);
    QVERIFY(!service.activeVehicle());
    QVERIFY(service.vehicles().isEmpty());
}

void HostVehicleServicesTest::_missionServiceReadyAndSave_test()
{
    QGCMissionServiceImpl service;
    const int vehicleId = vehicle()->id();

    QVERIFY(waitForCondition([this] { return vehicle()->initialPlanRequestComplete(); },
                             TestTimeout::longMs(), QStringLiteral("initialPlanRequestComplete")));
    QVERIFY(service.missionReady(vehicleId));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString planPath = tempDir.filePath(QStringLiteral("snapshot.plan"));

    QVERIFY(service.saveVehicleMissionToFile(vehicleId, planPath));
    QVERIFY(QFileInfo::exists(planPath));
    QVERIFY(QFileInfo(planPath).size() > 0);

    // The snapshot must be a valid .plan JSON document
    QFile planFile(planPath);
    QVERIFY(planFile.open(QIODevice::ReadOnly));
    QVERIFY(QJsonDocument::fromJson(planFile.readAll()).isObject());
}

void HostVehicleServicesTest::_missionServiceRelaysReadyChanged_test()
{
    // The service seeds its per-vehicle watch from the already-connected vehicle
    QGCMissionServiceImpl service;
    QSignalSpy readySpy(&service, &QGCMissionService::missionReadyChanged);

    // Fire the vehicle's signal through the metaobject — the service must
    // relay it keyed by vehicle id
    QVERIFY(QMetaObject::invokeMethod(vehicle(), "initialPlanRequestCompleteChanged", Q_ARG(bool, true)));

    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.at(0).at(0).toInt(), vehicle()->id());
    QCOMPARE(readySpy.at(0).at(1).toBool(), true);
}

void HostVehicleServicesTest::_missionServiceUnknownVehicle_test()
{
    QGCMissionServiceImpl service;
    const int bogusId = vehicle()->id() + 1;

    QVERIFY(!service.missionReady(bogusId));
    QVERIFY(!service.saveVehicleMissionToFile(bogusId, QStringLiteral("/nonexistent/out.plan")));
}

UT_REGISTER_TEST(HostVehicleServicesTest, TestLabel::Integration, TestLabel::Vehicle)
