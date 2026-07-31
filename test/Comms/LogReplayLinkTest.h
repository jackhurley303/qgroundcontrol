#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QTemporaryDir>

#include "UnitTest.h"

class LogReplayWorker;
class QSignalSpy;

/// Tests for LogReplayWorker tlog file loading, playback control and seek state
/// resolution. Logs with uninterpretable trailing bytes (e.g. not closed cleanly due to
/// crash/power loss) must still replay (issue #14210). The deferred-stream tests cover the
/// playback lifecycle a caller drives itself via beginStream(). The timeline tests drive
/// the worker directly against a synthetic log with a known event schedule, with no plugin
/// present, since seek state resolution must work for any consumer.
class LogReplayLinkTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _testCleanLogLoads();
    void _testTrailingBytesIgnored_data();
    void _testTrailingBytesIgnored();
    void _testGarbageOnlyLogFails();
    void _testStreamStartsWhenNotDeferred();
    void _testDeferredStreamLifecycle();
    void _testTimelineBuiltIncrementally();
    void _testSeekResolvesMissionState();
    void _testSeekResolvesParamState();
    void _testSeekToStartAndPastEnd();
    void _testSeekOntoRepeatedTimestamp();

private:
    QString _writeLogFile(const QByteArray& contents);

    /// Seeks and reports the log time the seek actually landed on. Percent maps to a file
    /// position rather than to a time, so every expectation must key off where the seek
    /// landed rather than off the requested percent. Fails the test if the seek reported no
    /// landing at all, rather than handing back a sentinel which would satisfy the callers'
    /// range checks.
    static void _seekTo(LogReplayWorker& worker, qreal percentComplete, QSignalSpy& timeSpy,
                        uint32_t& landedSecs);

    QTemporaryDir _tempDir;
};
