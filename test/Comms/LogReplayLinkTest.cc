#include "LogReplayLinkTest.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QtEndian>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "LogReplayLink.h"
#include "MAVLinkLib.h"
#include "SyntheticTlog.h"

namespace {

/// Returns a valid tlog stream: cMessages HEARTBEATs, each preceded by a big-endian
/// microsecond timestamp, spaced one second apart starting at baseTimeUSecs. Deliberately
/// minimal - the log-format cases below assert against exact byte layout, where the richer
/// SyntheticTlog schedule used by the timeline cases would only obscure things.
QByteArray buildTlogBytes(int cMessages, quint64 baseTimeUSecs)
{
    QByteArray tlog;

    for (int i = 0; i < cMessages; i++) {
        const quint64 timestampUSecs = qToBigEndian<quint64>(baseTimeUSecs + (i * 1000000ULL));
        (void) tlog.append(reinterpret_cast<const char*>(&timestampUSecs), sizeof(timestampUSecs));

        mavlink_message_t msg{};
        (void) mavlink_msg_heartbeat_pack(1, MAV_COMP_ID_AUTOPILOT1, &msg, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4, 0, 0,
                                          MAV_STATE_ACTIVE);

        uint8_t buffer[MAVLINK_MAX_PACKET_LEN]{};
        const int cBuffer = mavlink_msg_to_send_buffer(buffer, &msg);
        (void) tlog.append(reinterpret_cast<const char*>(buffer), cBuffer);
    }

    return tlog;
}

const ParamSeekValue* findResolvedParam(const QList<ParamSeekValue>& resolved, const QString& paramId)
{
    for (const ParamSeekValue& seekValue : resolved) {
        if (seekValue.paramId == paramId) {
            return &seekValue;
        }
    }

    return nullptr;
}

}  // namespace

QString LogReplayLinkTest::_writeLogFile(const QByteArray& contents)
{
    if (!_tempDir.isValid()) {
        qCritical() << "Temp dir invalid:" << _tempDir.errorString();
        return QString();
    }

    const QString filename = _tempDir.filePath(QStringLiteral("%1.tlog").arg(QTest::currentTestFunction()));

    QFile file(filename);
    if (!file.open(QFile::WriteOnly)) {
        qCritical() << "Failed to open" << filename << ":" << file.errorString();
        return QString();
    }
    if (file.write(contents) != contents.size()) {
        qCritical() << "Short write to" << filename << ":" << file.errorString();
        return QString();
    }

    return filename;
}

void LogReplayLinkTest::_testCleanLogLoads()
{
    const quint64 baseTimeUSecs = 1700000000000000ULL;
    const QString filename = _writeLogFile(buildTlogBytes(3, baseTimeUSecs));
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy connectedSpy(&worker, &LogReplayWorker::connected);
    QSignalSpy errorSpy(&worker, &LogReplayWorker::errorOccurred);
    QSignalSpy statsSpy(&worker, &LogReplayWorker::logFileStats);

    worker.connectToLog();

    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(statsSpy.count(), 1);
    QCOMPARE(statsSpy.first().first().toUInt(), 2u);  // 3 messages, 1 second apart

    worker.pause();
    worker.disconnectFromLog();
}

void LogReplayLinkTest::_testTrailingBytesIgnored_data()
{
    QTest::addColumn<int>("cTrailingBytes");
    QTest::addColumn<bool>("expectWarning");

    // Trailing junk smaller than the 8 byte timestamp is never read (loop condition), so no warning.
    // Anything at least timestamp-sized is read as a candidate timestamp, detected as incomplete
    // and warned about.
    QTest::newRow("1 byte") << 1 << false;
    QTest::newRow("7 bytes") << 7 << false;
    QTest::newRow("8 bytes (timestamp size)") << 8 << true;
    QTest::newRow("9 bytes") << 9 << true;
    QTest::newRow("100 bytes") << 100 << true;
}

void LogReplayLinkTest::_testTrailingBytesIgnored()
{
    QFETCH(int, cTrailingBytes);
    QFETCH(bool, expectWarning);

    // Issue #14210: a log which was not closed cleanly (crash/power loss) can end with
    // trailing bytes which are not part of any MAVLink message. Those bytes must be
    // ignored rather than failing the load with a corrupt file error.
    const quint64 baseTimeUSecs = 1700000000000000ULL;
    QByteArray tlog = buildTlogBytes(3, baseTimeUSecs);
    (void) tlog.append(QByteArray(cTrailingBytes, '\0'));

    const QString filename = _writeLogFile(tlog);
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy connectedSpy(&worker, &LogReplayWorker::connected);
    QSignalSpy errorSpy(&worker, &LogReplayWorker::errorOccurred);
    QSignalSpy statsSpy(&worker, &LogReplayWorker::logFileStats);

    if (expectWarning) {
        expectLogMessage("Comms.LogReplayLink", QtWarningMsg,
                         QRegularExpression(QStringLiteral("Ignoring trailing bytes")));
    }
    worker.connectToLog();
    if (expectWarning) {
        verifyExpectedLogMessage();
    }

    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(statsSpy.count(), 1);
    QCOMPARE(statsSpy.first().first().toUInt(), 2u);  // trailing bytes must not affect duration

    worker.pause();
    worker.disconnectFromLog();
}

void LogReplayLinkTest::_testGarbageOnlyLogFails()
{
    // A log without any complete MAVLink message must still be rejected as corrupt.
    const QString filename = _writeLogFile(QByteArray(100, '\0'));
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy connectedSpy(&worker, &LogReplayWorker::connected);
    QSignalSpy errorSpy(&worker, &LogReplayWorker::errorOccurred);

    expectLogMessage("Comms.LogReplayLink", QtWarningMsg,
                     QRegularExpression(QStringLiteral("No complete MAVLink message found")));
    worker.connectToLog();
    verifyExpectedLogMessage();

    QCOMPARE(connectedSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.first().first().toString().contains(QStringLiteral("corrupt or empty")));
}

void LogReplayLinkTest::_testStreamStartsWhenNotDeferred()
{
    // Default behaviour: connectToLog bootstraps the vehicle and streams straight on.
    const quint64 baseTimeUSecs = 1700000000000000ULL;
    const QString filename = _writeLogFile(buildTlogBytes(5, baseTimeUSecs));
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);
    QVERIFY(!config.deferStreamStart());

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy startedSpy(&worker, &LogReplayWorker::playbackStarted);

    worker.connectToLog();

    QCOMPARE(startedSpy.count(), 1);
    QVERIFY(worker.isPlaying());

    worker.pause();
    worker.disconnectFromLog();
}

void LogReplayLinkTest::_testDeferredStreamLifecycle()
{
    // deferStreamStart lets a caller (e.g. a plugin driving replay through the host
    // services) initialize against the bootstrapped vehicle before playback runs.
    const quint64 baseTimeUSecs = 1700000000000000ULL;
    const QString filename = _writeLogFile(buildTlogBytes(5, baseTimeUSecs));
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);
    config.setDeferStreamStart(true);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy connectedSpy(&worker, &LogReplayWorker::connected);
    QSignalSpy startedSpy(&worker, &LogReplayWorker::playbackStarted);
    QSignalSpy pausedSpy(&worker, &LogReplayWorker::playbackPaused);
    QSignalSpy atEndSpy(&worker, &LogReplayWorker::playbackAtEnd);
    QSignalSpy dataSpy(&worker, &LogReplayWorker::dataReceived);

    // Bootstrap only: the vehicle-creating heartbeat is emitted, the stream is not started.
    worker.connectToLog();
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(startedSpy.count(), 0);
    QVERIFY(!worker.isPlaying());
    QCOMPARE(dataSpy.count(), 1);

    worker.beginStream();
    QCOMPARE(startedSpy.count(), 1);
    QVERIFY(worker.isPlaying());

    worker.pause();
    QCOMPARE(pausedSpy.count(), 1);
    QVERIFY(!worker.isPlaying());

    // A speed change must never resume a paused session, or it would defeat the
    // deferred start it is allowed to precede.
    worker.setPlaybackSpeed(10);
    QVERIFY(!worker.isPlaying());

    worker.play();
    QVERIFY(worker.isPlaying());
    QTRY_VERIFY_WITH_TIMEOUT(atEndSpy.count() == 1, 5000);
    QVERIFY(!worker.isPlaying());

    // Restarting from the end rewinds and streams again rather than sitting at EOF.
    const int dataAtEnd = dataSpy.count();
    worker.play();
    QVERIFY(worker.isPlaying());
    QTRY_VERIFY_WITH_TIMEOUT(dataSpy.count() > dataAtEnd, 5000);

    worker.pause();
    worker.disconnectFromLog();
}

void LogReplayLinkTest::_seekTo(LogReplayWorker& worker, qreal percentComplete, QSignalSpy& timeSpy,
                                uint32_t& landedSecs)
{
    timeSpy.clear();
    worker.movePlayhead(percentComplete);

    QVERIFY(!timeSpy.isEmpty());
    landedSecs = timeSpy.last().first().toUInt();
}

void LogReplayLinkTest::_testTimelineBuiltIncrementally()
{
    // Opening a log must not scan it: the mission and parameter timelines are built from
    // the reads playback and seek already perform, so open cost does not grow with the
    // length of the log. Coverage tracks how far the session has read.
    const QString filename = _writeLogFile(SyntheticTlog::canonicalFlight());
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);
    config.setDeferStreamStart(true);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy timeSpy(&worker, &LogReplayWorker::currentLogTimeSecs);

    worker.connectToLog();

    // Bootstrap stops at the first heartbeat, which is the log's very first message.
    QCOMPARE(worker.timelineCoverageUSecs(), SyntheticTlog::kBaseTimeUSecs);

    uint32_t landedForward = 0;
    _seekTo(worker, 40.0, timeSpy, landedForward);
    QVERIFY(landedForward > 10);
    const quint64 coverageAfterForward = worker.timelineCoverageUSecs();
    // Coverage reaches the last message before the seek target - and, crucially, stops
    // there rather than running on to the end of the log. The landing time is truncated to
    // whole seconds, so the bounds allow the second it landed within.
    const quint64 landedForwardUSecs = SyntheticTlog::kBaseTimeUSecs + (landedForward * 1000000ULL);
    QVERIFY(coverageAfterForward > landedForwardUSecs - 1000000ULL);
    QVERIFY(coverageAfterForward < landedForwardUSecs + 1000000ULL);

    // Seeking backward resolves from the timeline already built - it must not rescan.
    uint32_t landedBackward = 0;
    _seekTo(worker, 10.0, timeSpy, landedBackward);
    QVERIFY(landedBackward < landedForward);
    QCOMPARE(worker.timelineCoverageUSecs(), coverageAfterForward);

    // Seeking beyond what has been read extends coverage again.
    uint32_t landedFurther = 0;
    _seekTo(worker, 90.0, timeSpy, landedFurther);
    QVERIFY(landedFurther > landedForward);
    QVERIFY(worker.timelineCoverageUSecs() > coverageAfterForward);

    worker.disconnectFromLog();
}

void LogReplayLinkTest::_testSeekResolvesMissionState()
{
    // Mission state at a seek target is whatever the last GCS upload before it left, with
    // no plugin involved in either recording or resolving it.
    const QString filename = _writeLogFile(SyntheticTlog::canonicalFlight());
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);
    config.setDeferStreamStart(true);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy timeSpy(&worker, &LogReplayWorker::currentLogTimeSecs);
    QSignalSpy missionSpy(&worker, &LogReplayWorker::replaySeekMissionResolved);

    worker.connectToLog();

    const auto resolvedMission = [&missionSpy]() {
        return missionSpy.last().first().value<MissionItemsByType>();
    };

    // Before the first upload: no mission events yet, so no type resolves.
    uint32_t landed = 0;
    _seekTo(worker, 10.0, timeSpy, landed);
    QVERIFY(landed > 1 && landed < SyntheticTlog::kMissionUpload1Secs);
    QCOMPARE(missionSpy.count(), 1);
    QVERIFY(resolvedMission().isEmpty());

    // Between the two uploads: the first upload's 2 items.
    _seekTo(worker, 40.0, timeSpy, landed);
    QVERIFY(landed > SyntheticTlog::kMissionUpload1Secs && landed < SyntheticTlog::kMissionUpload2Secs);
    QCOMPARE(resolvedMission().value(MAV_MISSION_TYPE_MISSION).count(), 2);

    // After the second upload: its 3 items.
    _seekTo(worker, 90.0, timeSpy, landed);
    QVERIFY(landed > SyntheticTlog::kMissionUpload2Secs);
    QCOMPARE(resolvedMission().value(MAV_MISSION_TYPE_MISSION).count(), 3);

    // Seeking backward must resolve the earlier state again, not leave the later one.
    _seekTo(worker, 40.0, timeSpy, landed);
    QVERIFY(landed > SyntheticTlog::kMissionUpload1Secs && landed < SyntheticTlog::kMissionUpload2Secs);
    QCOMPARE(resolvedMission().value(MAV_MISSION_TYPE_MISSION).count(), 2);

    _seekTo(worker, 3.0, timeSpy, landed);
    QVERIFY(landed < SyntheticTlog::kMissionUpload1Secs);
    QVERIFY(resolvedMission().isEmpty());

    worker.disconnectFromLog();
}

void LogReplayLinkTest::_testSeekResolvesParamState()
{
    // Every parameter the log has changed resolves to its value at the seek target, or to
    // resetToInitial when the target precedes its first recorded value.
    const QString filename = _writeLogFile(SyntheticTlog::canonicalFlight());
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);
    config.setDeferStreamStart(true);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy timeSpy(&worker, &LogReplayWorker::currentLogTimeSecs);
    QSignalSpy paramSpy(&worker, &LogReplayWorker::replaySeekParamResolved);

    worker.connectToLog();

    const auto resolvedParams = [&paramSpy]() {
        return paramSpy.last().at(1).value<QList<ParamSeekValue>>();
    };

    // Between the two PARAM_A changes, and before PARAM_C is ever seen.
    uint32_t landed = 0;
    _seekTo(worker, 40.0, timeSpy, landed);
    QVERIFY(landed > SyntheticTlog::kParamAChange1Secs && landed < SyntheticTlog::kParamAChange2Secs);
    QCOMPARE(paramSpy.count(), 1);
    QCOMPARE(paramSpy.last().first().toInt(), static_cast<int>(SyntheticTlog::kSysId));

    QList<ParamSeekValue> resolved = resolvedParams();
    const ParamSeekValue* paramA = findResolvedParam(resolved, QStringLiteral("PARAM_A"));
    QVERIFY(paramA);
    QVERIFY(!paramA->resetToInitial);
    QCOMPARE(paramA->rawValue, 5.0f);
    QCOMPARE(paramA->compId, static_cast<int>(MAV_COMP_ID_AUTOPILOT1));

    const ParamSeekValue* paramB = findResolvedParam(resolved, QStringLiteral("PARAM_B"));
    QVERIFY(paramB);
    QCOMPARE(paramB->rawValue, 100.0f);

    // PARAM_C is first seen at t=20.5, which this seek has not read yet, so it is absent
    // rather than resolved. Nothing has applied it either, so there is nothing to revert.
    const ParamSeekValue* paramC = findResolvedParam(resolved, QStringLiteral("PARAM_C"));
    QVERIFY(!paramC);

    // After both later changes: PARAM_C now has a value of its own.
    _seekTo(worker, 90.0, timeSpy, landed);
    QVERIFY(landed > SyntheticTlog::kParamAChange2Secs);
    resolved = resolvedParams();
    paramA = findResolvedParam(resolved, QStringLiteral("PARAM_A"));
    QVERIFY(paramA);
    QCOMPARE(paramA->rawValue, 9.0f);
    paramC = findResolvedParam(resolved, QStringLiteral("PARAM_C"));
    QVERIFY(paramC);
    QVERIFY(!paramC->resetToInitial);
    QCOMPARE(paramC->rawValue, 7.0f);

    // Seeking back before the first change reverts PARAM_A to its flood value, and puts
    // PARAM_C back to resetToInitial - a stale value here is what a blind seek leaves.
    // Landing time is whole seconds, so this target must sit clear of the t=0.1 flood for
    // the expectation to be unambiguous.
    _seekTo(worker, 20.0, timeSpy, landed);
    QVERIFY(landed > SyntheticTlog::kParamFloodSecs && landed < SyntheticTlog::kParamAChange1Secs);
    resolved = resolvedParams();
    paramA = findResolvedParam(resolved, QStringLiteral("PARAM_A"));
    QVERIFY(paramA);
    QVERIFY(!paramA->resetToInitial);
    QCOMPARE(paramA->rawValue, 1.0f);
    paramC = findResolvedParam(resolved, QStringLiteral("PARAM_C"));
    QVERIFY(paramC);
    QVERIFY(paramC->resetToInitial);

    worker.disconnectFromLog();
}

void LogReplayLinkTest::_testSeekToStartAndPastEnd()
{
    const QString filename = _writeLogFile(SyntheticTlog::canonicalFlight());
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);
    config.setDeferStreamStart(true);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy timeSpy(&worker, &LogReplayWorker::currentLogTimeSecs);
    QSignalSpy missionSpy(&worker, &LogReplayWorker::replaySeekMissionResolved);
    QSignalSpy paramSpy(&worker, &LogReplayWorker::replaySeekParamResolved);

    worker.connectToLog();

    // Read most of the log first, so seeking back to the start has state to undo.
    uint32_t landed = 0;
    _seekTo(worker, 90.0, timeSpy, landed);
    QVERIFY(landed > SyntheticTlog::kParamAChange2Secs);

    _seekTo(worker, 0.0, timeSpy, landed);
    QCOMPARE(landed, 0u);
    QVERIFY(missionSpy.last().first().value<MissionItemsByType>().isEmpty());

    const QList<ParamSeekValue> atStart = paramSpy.last().at(1).value<QList<ParamSeekValue>>();
    QVERIFY(!atStart.isEmpty());
    for (const ParamSeekValue& seekValue : atStart) {
        QVERIFY2(seekValue.resetToInitial, qPrintable(seekValue.paramId));
    }

    // Past the end clamps to the end rather than erroring or resolving a partial state.
    QSignalSpy errorSpy(&worker, &LogReplayWorker::errorOccurred);
    _seekTo(worker, 150.0, timeSpy, landed);
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(landed > SyntheticTlog::kParamAChange2Secs);
    QCOMPARE(missionSpy.last().first().value<MissionItemsByType>().value(MAV_MISSION_TYPE_MISSION).count(), 3);
    const ParamSeekValue* atEnd = findResolvedParam(paramSpy.last().at(1).value<QList<ParamSeekValue>>(),
                                                    QStringLiteral("PARAM_A"));
    QVERIFY(atEnd);
    QCOMPARE(atEnd->rawValue, 9.0f);

    worker.disconnectFromLog();
}

void LogReplayLinkTest::_testSeekOntoRepeatedTimestamp()
{
    // Frames decoded from a single link read all carry the same millisecond-resolution
    // timestamp, so a seek routinely lands partway through a run of messages sharing one
    // time. Everything in that run before the landing point has been played, so it must
    // still reach the timelines - and must not be skipped for good once the playhead moves
    // past it.
    SyntheticTlog builder;
    (void) builder.flight(0, 1);
    (void) builder.paramValue(5.0, QStringLiteral("BURST_PARAM"), 42.0f);
    (void) builder.heartbeatBurst(5.0, 300);
    (void) builder.flight(10, 11);

    const QString filename = _writeLogFile(builder.bytes());
    QVERIFY(!filename.isEmpty());

    LogReplayConfiguration config(QStringLiteral("LogReplayLinkTest"));
    config.setLogFilename(filename);
    config.setDeferStreamStart(true);

    LogReplayWorker worker(&config);
    worker.setup();

    QSignalSpy timeSpy(&worker, &LogReplayWorker::currentLogTimeSecs);
    QSignalSpy paramSpy(&worker, &LogReplayWorker::replaySeekParamResolved);

    worker.connectToLog();

    // The burst dominates the file, so this lands well inside it - on a message carrying
    // the same timestamp as the parameter which opens the run.
    uint32_t landed = 0;
    _seekTo(worker, 50.0, timeSpy, landed);
    QCOMPARE(landed, 5u);

    QCOMPARE(paramSpy.count(), 1);
    const ParamSeekValue* burstParam = findResolvedParam(paramSpy.last().at(1).value<QList<ParamSeekValue>>(),
                                                         QStringLiteral("BURST_PARAM"));
    QVERIFY(burstParam);
    QVERIFY(!burstParam->resetToInitial);
    QCOMPARE(burstParam->rawValue, 42.0f);

    worker.disconnectFromLog();
}

UT_REGISTER_TEST(LogReplayLinkTest, TestLabel::Unit, TestLabel::Comms)
