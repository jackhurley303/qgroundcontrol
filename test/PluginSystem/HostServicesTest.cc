#include "HostServicesTest.h"

#include <QtTest/QSignalSpy>

#include "HostServices/QGCHostServicesImpl.h"
#include "HostServices/QGCReplayServiceImpl.h"
#include "HostServices/QGCTelemetryLoggingServiceImpl.h"
#include "MAVLinkProtocol.h"
#include "QGCReplayService.h"
#include "QGCTelemetryLoggingService.h"

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

    // Plugins acquire by id and qobject_cast to the SDK interface: the casts
    // succeeding pins the interfaces' metaobject anchors in the SDK library.
    QObject* replayObj = services.service(QGCReplayServiceId);
    QVERIFY(replayObj);
    QVERIFY(qobject_cast<QGCReplayService*>(replayObj));

    QObject* loggingObj = services.service(QGCTelemetryLoggingServiceId);
    QVERIFY(loggingObj);
    QVERIFY(qobject_cast<QGCTelemetryLoggingService*>(loggingObj));

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

UT_REGISTER_TEST(HostServicesTest, TestLabel::Unit)
