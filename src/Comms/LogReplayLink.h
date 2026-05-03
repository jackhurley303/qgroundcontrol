#pragma once

#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "MAVLinkLib.h"

#include <QtCore/QFile>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

#include <atomic>

class QTimer;

Q_DECLARE_METATYPE(QList<mavlink_mission_item_int_t>)
using MissionItemsByType = QMap<int, QList<mavlink_mission_item_int_t>>;
Q_DECLARE_METATYPE(MissionItemsByType)

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

signals:
    void filenameChanged();

private:
    QString _logFilename;
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
    void _readUntilHeartbeat();
    bool _loadLogFile();
    void _resetPlaybackToBeginning();
    void _signalCurrentLogTimeSecs();
    void _detectReplayMissionUpload(const mavlink_message_t &msg);
    void _buildMissionTimeline();

    struct MissionSnapshot {
        quint64 timeUSecs;
        QList<mavlink_mission_item_int_t> items;  // empty = cleared
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

    // Pre-scanned timeline of mission state changes, keyed by MAV_MISSION_TYPE
    QMap<uint8_t, QList<MissionSnapshot>> _missionTimeline;
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
