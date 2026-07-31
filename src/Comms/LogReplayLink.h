#pragma once

#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "MAVLinkLib.h"

#include <QtCore/QFile>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMap>
#include <QtCore/QPair>
#include <QtCore/QSet>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

#include <atomic>

class QTimer;

Q_DECLARE_METATYPE(QList<mavlink_mission_item_int_t>)
using MissionItemsByType = QMap<int, QList<mavlink_mission_item_int_t>>;
Q_DECLARE_METATYPE(MissionItemsByType)

/// Resolved parameter value emitted by replaySeekParamResolved.
/// When resetToInitial is true the target precedes the parameter's first recorded value,
/// so the receiver should revert it to its initial value. rawValue and paramType then
/// carry that first recorded value, which is what a receiver with no separate params file
/// has to revert to.
struct ParamSeekValue {
    int     compId         = 0;
    QString paramId;
    float   rawValue       = 0.0f;
    uint8_t paramType      = 0;        ///< MAV_PARAM_TYPE
    bool    resetToInitial = false;
};
Q_DECLARE_METATYPE(QList<ParamSeekValue>)

Q_DECLARE_LOGGING_CATEGORY(LogReplayLinkLog)


/*===========================================================================*/

class LogReplayConfiguration : public LinkConfiguration
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    Q_PROPERTY(QString filename READ logFilename WRITE setLogFilename NOTIFY filenameChanged)

public:
    explicit LogReplayConfiguration(const QString &name, QObject *parent = nullptr);
    explicit LogReplayConfiguration(const LogReplayConfiguration *copy, QObject *parent = nullptr);
    virtual ~LogReplayConfiguration();

    LinkType type() const override { return LinkConfiguration::TypeLogReplay; }
    void copyFrom(const LinkConfiguration *source) override;
    void loadSettings(QSettings &settings, const QString &root) override;
    void saveSettings(QSettings &settings, const QString &root) const override;
    QString settingsURL() const override { return QStringLiteral("LogReplaySettings.qml"); }
    QString settingsTitle() const override { return tr("Log Replay Link Settings"); }

    QString logFilenameShort() const;
    QString logFilename() const { return _logFilename; }
    void setLogFilename(const QString &logFilename);

    /// true: the link bootstraps the vehicle but does not start streaming until the
    /// caller calls LogReplayLink::beginStream(). Used by callers which must complete
    /// their own initialization against the newly created vehicle first.
    bool deferStreamStart() const { return _deferStreamStart; }
    void setDeferStreamStart(bool defer) { _deferStreamStart = defer; }

signals:
    void filenameChanged();

private:
    QString _logFilename;
    /// Intent of the caller which started this session only - deliberately not persisted
    /// to settings nor carried across copyFrom(), so any other use streams immediately.
    bool _deferStreamStart = false;
};

/*===========================================================================*/

class LogReplayWorker : public QObject
{
    Q_OBJECT

public:
    explicit LogReplayWorker(const LogReplayConfiguration *config, QObject *parent = nullptr);
    ~LogReplayWorker();

    bool isConnected() const { return _isConnected; }
    bool isPlaying() const;

    /// Log time through which the mission and parameter timelines have been scanned.
    /// The timelines are built incrementally from the reads playback and seek already
    /// perform, so this tracks how far the session has progressed through the log
    /// rather than the size of the log itself. A parameter or mission which the session
    /// has not read yet is therefore absent from a seek resolution rather than reported
    /// as needing a reset - nothing has applied it, so there is nothing to revert.
    quint64 timelineCoverageUSecs() const { return _timelineCoverageUSecs; }

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &errorString);
    void dataReceived(const QByteArray &data);
    void logFileStats(uint32_t logDurationSecs);
    void playbackStarted();
    void playbackPaused();
    void playbackAtEnd();
    void playbackPercentCompleteChanged(qreal percentComplete);
    void currentLogTimeSecs(uint32_t secs);
    void seekStarted();
    void seekReplayComplete(QList<QGeoCoordinate> coords);
    void seekFlightStatsReady(double flightTimeSecs, double flightDistanceMeters);
    void playbackSpeedChanged(qreal speed);
    /// Emitted when the tlog contains a complete GCS→vehicle mission upload sequence
    /// (MISSION_COUNT + all MISSION_ITEM_INT from a non-autopilot compid). missionType is MAV_MISSION_TYPE.
    void replayMissionUploaded(int missionType, QList<mavlink_mission_item_int_t> items);
    /// Emitted after seek (and on restart-from-end) with the last known mission state per type
    /// at or before the seek point. Types absent from the map had no events — initial plan applies.
    void replaySeekMissionResolved(QMap<int, QList<mavlink_mission_item_int_t>> resolvedByType);
    /// Emitted after seek (and on restart-from-end) with the resolved parameter values for
    /// every parameter that appeared in the tlog. resetToInitial=true means the param should
    /// be reverted to its params-file value (seek target is before its first recorded change).
    void replaySeekParamResolved(int sysId, QList<ParamSeekValue> resolved);

public slots:
    void setup();
    void connectToLog();
    void disconnectFromLog();
    void play();
    void pause();
    void beginStream();
    void setPlaybackSpeed(qreal playbackSpeed);
    void movePlayhead(qreal percentComplete);

private slots:
    void _readNextLogEntry();

private:
    quint64 _parseTimestamp(const QByteArray &bytes);
    quint64 _seekToNextMavlinkMessage(mavlink_message_t &nextMsg);
    quint64 _findLastTimestamp();
    quint64 _readNextMavlinkMessage(QByteArray &bytes);
    quint64 _readNextMavlinkMessage(QByteArray &bytes, mavlink_message_t &outMsg);
    /// Reads up to and including the first autopilot HEARTBEAT, which bootstraps vehicle
    /// creation without starting the full stream.
    ///     @return true if that heartbeat was reached and playback can stream on from there
    bool _readUntilHeartbeat();
    /// Holds off new connections and live telemetry logging for the duration of a replay
    void _setHostReplaySuspended(bool suspended);
    bool _loadLogFile();
    void _resetPlaybackToBeginning();
    void _signalCurrentLogTimeSecs();
    void _detectReplayMissionUpload(const mavlink_message_t &msg);
    /// Folds one message into the mission and parameter timelines. Called from every
    /// forward read of the log - bootstrap, playback and the seek replay pass - so the
    /// timelines cost no file reads of their own. Messages at or before the furthest
    /// point already scanned are ignored, making repeated and backward seeks free.
    ///     @param timeUSecs Timestamp of msg itself, not of the message which follows it
    void _recordTimelineEvent(const mavlink_message_t &msg, quint64 timeUSecs);
    void _emitParamSeekReset();

    struct MissionSnapshot {
        quint64 timeUSecs;
        QList<mavlink_mission_item_int_t> items;  // empty = cleared
    };

    struct ParamTimelineEntry {
        quint64 timeUSecs;
        float   rawValue;
        uint8_t paramType;
    };

    const LogReplayConfiguration *_logReplayConfig = nullptr;
    QTimer *_readTickTimer = nullptr;

    bool _isConnected = false;
    uint8_t _mavlinkChannel = 0;

    quint64 _logCurrentTimeUSecs = 0;
    quint64 _logStartTimeUSecs = 0;
    quint64 _logEndTimeUSecs = 0;
    quint64 _logDurationUSecs = 0;

    qreal _playbackSpeed = 1;
    quint64 _playbackStartTimeMSecs = 0;
    quint64 _playbackStartLogTimeUSecs = 0;

    QFile _logFile;
    quint64 _logFileSize = 0;

    static constexpr size_t kTimestamp = sizeof(quint64);

    // Live playback upload detection state
    QMap<uint8_t, QList<mavlink_mission_item_int_t>> _pendingUploadItems;
    QMap<uint8_t, uint16_t>                          _pendingUploadCount;

    // Timeline of mission state changes, keyed by MAV_MISSION_TYPE
    QMap<uint8_t, QList<MissionSnapshot>> _missionTimeline;

    // Timeline of parameter value changes.
    // Outer key: sysId. Inner key: (compId, paramId). Value: chronologically sorted entries.
    // Only records actual value changes (initial download flood filtered out).
    QMap<uint8_t, QMap<QPair<int,QString>, QList<ParamTimelineEntry>>> _paramTimelineByKey;

    // Incremental timeline scan state. The position high water mark is what makes the
    // scan free: it is exact where a timestamp comparison would not be, since several
    // messages can share a microsecond. The mission accumulators persist across reads
    // so an upload split by the high water mark still completes.
    qint64  _timelineScannedThroughPos = 0;
    quint64 _timelineCoverageUSecs = 0;
    QMap<uint8_t, QList<mavlink_mission_item_int_t>> _timelinePendingItems;
    QMap<uint8_t, uint16_t>                          _timelinePendingCount;
    QMap<uint8_t, QMap<QPair<int,QString>, float>>   _timelineLastParamValue;
};

/*===========================================================================*/

class LogReplayLink : public LinkInterface
{
    Q_OBJECT

public:
    explicit LogReplayLink(SharedLinkConfigurationPtr &config, QObject *parent = nullptr);
    virtual ~LogReplayLink();

    bool isConnected() const override;
    void disconnect() override;
    bool isLogReplay() const final { return true; }

    bool isPlaying() const;
    void play();
    void pause();
    void beginStream();
    void setPlaybackSpeed(qreal playbackSpeed);
    void movePlayhead(qreal percentComplete);

signals:
    void logFileStats(uint32_t logDurationSecs);
    void playbackStarted();
    void playbackPaused();
    void playbackAtEnd();
    void playbackPercentCompleteChanged(qreal percentComplete);
    void currentLogTimeSecs(uint32_t secs);
    void seekStarted();
    void seekReplayComplete(QList<QGeoCoordinate> coords);
    void seekFlightStatsReady(double flightTimeSecs, double flightDistanceMeters);
    void playbackSpeedChanged(qreal speed);
    void replayMissionUploaded(int missionType, QList<mavlink_mission_item_int_t> items);
    void replaySeekMissionResolved(QMap<int, QList<mavlink_mission_item_int_t>> resolvedByType);
    void replaySeekParamResolved(int sysId, QList<ParamSeekValue> resolved);
    void replayPlanReloadRequested();

public slots:
    void requestPlanReload();

private slots:
    void _writeBytes(const QByteArray &bytes) override { Q_UNUSED(bytes); }
    void _onConnected();
    void _onDisconnected();
    void _onErrorOccurred(const QString &errorString);
    void _onDataReceived(const QByteArray &data);

private:
    bool _connect() override;

    const LogReplayConfiguration *_logReplayConfig = nullptr;
    LogReplayWorker *_worker = nullptr;
    QThread *_workerThread = nullptr;
    std::atomic<bool> _disconnectedEmitted{false};
};
