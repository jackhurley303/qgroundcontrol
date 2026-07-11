#include "HostServicesTest.h"

#include <QtTest/QSignalSpy>

#include "AppSettings.h"
#include "HostServices/QGCAppServiceImpl.h"
#include "HostServices/QGCHostServicesImpl.h"
#include "HostServices/QGCMissionServiceImpl.h"
#include "HostServices/QGCReplayServiceImpl.h"
#include "HostServices/QGCTelemetryLoggingServiceImpl.h"
#include "HostServices/QGCVehicleServiceImpl.h"
#include "MAVLinkProtocol.h"
#include "QGCAppService.h"
#include "QGCMissionService.h"
#include "QGCReplayService.h"
#include "QGCTelemetryLoggingService.h"
#include "QGCVehicleService.h"
#include "SettingsManager.h"

void HostServicesTest::_registryLookup_test()
{
    QGCHostServicesImpl services;
    QObject first;
    QObject second;

    services.registerService(QStringLiteral("test.first/1"), &first);
    services.registerService(QStringLiteral("test.second/1"), &second);

    QCOMPARE(services.service(QStringLiteral("test.first/1")), &first);
    QCOMPARE(services.service(QStringLiteral("test.second/1")), &second);
    QCOMPARE(services.service(QStringLiteral("test.unknown/1")), nullptr);
}

void HostServicesTest::_registryDuplicateFirstWins_test()
{
    QGCHostServicesImpl services;
    QObject first;
    QObject second;

    services.registerService(QStringLiteral("test.dup/1"), &first);
    services.registerService(QStringLiteral("test.dup/1"), &second);

    QCOMPARE(services.service(QStringLiteral("test.dup/1")), &first);
}

void HostServicesTest::_defaultServicesResolveAndCast_test()
{
    // Assembled the way QGCPluginManager assembles it for plugins
    QGCHostServicesImpl services;
    services.registerService(QGCReplayServiceId, new QGCReplayServiceImpl(&services));
    services.registerService(QGCTelemetryLoggingServiceId, new QGCTelemetryLoggingServiceImpl(&services));
    services.registerService(QGCVehicleServiceId, new QGCVehicleServiceImpl(&services));
    services.registerService(QGCMissionServiceId, new QGCMissionServiceImpl(&services));
    services.registerService(QGCAppServiceId, new QGCAppServiceImpl(&services));

    // Plugins acquire by id and qobject_cast to the SDK interface: the casts
    // succeeding pins the interfaces' metaobject anchors in the SDK library.
    QObject* replayObj = services.service(QGCReplayServiceId);
    QVERIFY(replayObj);
    QVERIFY(qobject_cast<QGCReplayService*>(replayObj));

    QObject* loggingObj = services.service(QGCTelemetryLoggingServiceId);
    QVERIFY(loggingObj);
    QVERIFY(qobject_cast<QGCTelemetryLoggingService*>(loggingObj));

    QObject* vehicleObj = services.service(QGCVehicleServiceId);
    QVERIFY(vehicleObj);
    QVERIFY(qobject_cast<QGCVehicleService*>(vehicleObj));

    QObject* missionObj = services.service(QGCMissionServiceId);
    QVERIFY(missionObj);
    QVERIFY(qobject_cast<QGCMissionService*>(missionObj));

    QObject* appObj = services.service(QGCAppServiceId);
    QVERIFY(appObj);
    QVERIFY(qobject_cast<QGCAppService*>(appObj));

    QCOMPARE(services.service(QStringLiteral("qgc.bogus/1")), nullptr);
}

void HostServicesTest::_telemetryLoggingDelegates_test()
{
    QGCTelemetryLoggingServiceImpl service;

    // Nothing records in the test environment: the reads must report the
    // wrapped singleton's idle state.
    QVERIFY(!service.tlogLogging());
    QVERIFY(!service.hasPendingLog());
    QVERIFY(service.pendingLogName().isEmpty());
}

void HostServicesTest::_telemetryLoggingRelaysSignals_test()
{
    QGCTelemetryLoggingServiceImpl service;
    QSignalSpy loggingSpy(&service, &QGCTelemetryLoggingService::tlogLoggingChanged);
    QSignalSpy pendingSpy(&service, &QGCTelemetryLoggingService::hasPendingLogChanged);

    // Fire the singleton's signals through the metaobject (signals are
    // invokable) — the wrapper must relay them as its own.
    QVERIFY(QMetaObject::invokeMethod(MAVLinkProtocol::instance(), "tlogLoggingChanged"));
    QVERIFY(QMetaObject::invokeMethod(MAVLinkProtocol::instance(), "hasPendingLogChanged"));

    QCOMPARE(loggingSpy.count(), 1);
    QCOMPARE(pendingSpy.count(), 1);
}

void HostServicesTest::_replayInactiveNoOps_test()
{
    QGCReplayServiceImpl service;
    QSignalSpy endedSpy(&service, &QGCReplayService::replayEnded);

    QVERIFY(!service.replayActive());

    // All controls must be safe no-ops without an active session
    service.play();
    service.pause();
    service.beginStream();
    service.setPlaybackSpeed(2.0);
    service.setPlaybackSpeed(0.0);  // rejected by the documented > 0 contract
    service.movePlayhead(50.0);
    service.requestPlanReload();
    service.stopReplay();

    QVERIFY(!service.replayActive());
    // stopReplay without a session must not report a session end
    QCOMPARE(endedSpy.count(), 0);
}

void HostServicesTest::_replayRejectsEmptyPath_test()
{
    QGCReplayServiceImpl service;

    QVERIFY(!service.startReplay(QString()));
    QVERIFY(!service.replayActive());
}

void HostServicesTest::_replayRegistriesSmoke_test()
{
    QGCReplayServiceImpl service;

    // Smoke only: the registries are write-only host state (private static
    // maps, no read API), so delegation can't be asserted — set then clear an
    // entry to exercise the path without leaking test state.
    service.registerReplayParamFile(1, QStringLiteral("/nonexistent/replay.params"));
    service.registerReplayParamFile(1, QString());
    service.registerReplayPlanFile(1, QStringLiteral("/nonexistent/replay.plan"));
    service.registerReplayPlanFile(1, QString());
}

void HostServicesTest::_appServiceDelegates_test()
{
    QGCAppServiceImpl service;
    AppSettings* appSettings = SettingsManager::instance()->appSettings();

    QCOMPARE(service.applicationName(), QCoreApplication::applicationName());
    QCOMPARE(service.organizationName(), QCoreApplication::organizationName());
    QCOMPARE(service.versionString(), QCoreApplication::applicationVersion());
    QCOMPARE(service.savePath(), appSettings->savePath()->rawValue().toString());
    QCOMPARE(service.telemetrySavePath(), appSettings->telemetrySavePath());
    QVERIFY(!service.telemetrySavePath().isEmpty());
}

void HostServicesTest::_appServiceRelaysSavePathsChanged_test()
{
    QGCAppServiceImpl service;
    QSignalSpy pathsSpy(&service, &QGCAppService::savePathsChanged);

    QVERIFY(QMetaObject::invokeMethod(SettingsManager::instance()->appSettings(), "savePathsChanged"));

    QCOMPARE(pathsSpy.count(), 1);
}

void HostServicesTest::_vehicleAndMissionServicesNoVehicle_test()
{
    // No vehicle is connected in this fixture: the empty-world reads and the
    // unknown-id refusals must hold. The connected-vehicle behavior lives in
    // HostVehicleServicesTest.
    QGCVehicleServiceImpl vehicleService;
    QVERIFY(!vehicleService.activeVehicle());
    QVERIFY(vehicleService.vehicles().isEmpty());

    QGCMissionServiceImpl missionService;
    QVERIFY(!missionService.missionReady(1));
    QVERIFY(!missionService.saveVehicleMissionToFile(1, QStringLiteral("/nonexistent/out.plan")));
}

UT_REGISTER_TEST(HostServicesTest, TestLabel::Unit)
