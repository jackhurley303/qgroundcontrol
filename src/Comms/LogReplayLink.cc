#include "LogReplayLink.h"
#include "LinkManager.h"
#include "MAVLinkLib.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QFileInfo>
#include <QtCore/QtEndian>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include <algorithm>

QGC_LOGGING_CATEGORY(LogReplayLinkLog, "Comms.LogReplayLink")

static const bool _missionItemListRegistered = []{
    qRegisterMetaType<QList<mavlink_mission_item_int_t>>();
    qRegisterMetaType<QMap<int, QList<mavlink_mission_item_int_t>>>();
    return true;
}();

/*===========================================================================*/

LogReplayConfiguration::LogReplayConfiguration(const QString &name, QObject *parent)
    : LinkConfiguration(name, parent)
{
    qCDebug(LogReplayLinkLog) << this;
}

LogReplayConfiguration::LogReplayConfiguration(const LogReplayConfiguration *copy, QObject *parent)
    : LinkConfiguration(copy, parent)
    , _logFilename(copy->logFilename())
{
    qCDebug(LogReplayLinkLog) << this;
}

LogReplayConfiguration::~LogReplayConfiguration()
{
    qCDebug(LogReplayLinkLog) << this;
}

void LogReplayConfiguration::copyFrom(const LinkConfiguration *source)
{
    LinkConfiguration::copyFrom(source);

    const LogReplayConfiguration *logReplaySource = qobject_cast<const LogReplayConfiguration*>(source);

    setLogFilename(logReplaySource->logFilename());
}

void LogReplayConfiguration::loadSettings(QSettings &settings, const QString &root)
{
    settings.beginGroup(root);

    setLogFilename(settings.value("logFilename", "").toString());

    settings.endGroup();
}

void LogReplayConfiguration::saveSettings(QSettings &settings, const QString &root) const
{
    settings.beginGroup(root);

    settings.setValue("logFilename", _logFilename);

    settings.endGroup();
}

QString LogReplayConfiguration::logFilenameShort() const
{
    return QFileInfo(_logFilename).fileName();
}

void LogReplayConfiguration::setLogFilename(const QString &logFilename)
{
    if (logFilename != _logFilename) {
        _logFilename = logFilename;
        emit filenameChanged();
    }
}

/*===========================================================================*/

LogReplayWorker::LogReplayWorker(const LogReplayConfiguration *config, QObject *parent)
    : QObject(parent)
    , _logReplayConfig(config)
{
    qCDebug(LogReplayLinkLog) << this;
}

LogReplayWorker::~LogReplayWorker()
{
    disconnectFromLog();

    qCDebug(LogReplayLinkLog) << this;
}

void LogReplayWorker::setup()
{
    if (!_readTickTimer) {
        _readTickTimer = new QTimer(this);
    }

    (void) connect(_readTickTimer, &QTimer::timeout, this, &LogReplayWorker::_readNextLogEntry);
}

void LogReplayWorker::connectToLog()
{
    if (isConnected()) {
        qCWarning(LogReplayLinkLog) << "Already connected";
        return;
    }

    if (MultiVehicleManager::instance()->activeVehicle()) {
        emit errorOccurred(tr("You must close all connections prior to replaying a log."));
        return;
    }

    if (!_loadLogFile()) {
        disconnectFromLog();
        return;
    }

    _isConnected = true;
    emit connected();

    // Read messages until the first HEARTBEAT is emitted. This bootstraps
    // vehicle creation on the main thread (params + plan init) without
    // starting the full tlog stream. beginStream() is called by
    // FlightReplayController after init completes.
    _readUntilHeartbeat();
}

void LogReplayWorker::disconnectFromLog()
{
    if (!isConnected()) {
        qCDebug(LogReplayLinkLog) << "Already disconnected";
        return;
    }

    qCDebug(LogReplayLinkLog) << "Disconnecting from log";

    if (_readTickTimer) {
        _readTickTimer->stop();
    }

    if (_logFile.isOpen()) {
        _logFile.close();
    }

    _isConnected = false;
    emit disconnected();
}

bool LogReplayWorker::isPlaying() const
{
    return (_readTickTimer && _readTickTimer->isActive());
}

void LogReplayWorker::play()
{
    LinkManager::instance()->setConnectionsSuspended(tr("Connect not allowed during Flight Data replay."));
    MAVLinkProtocol::instance()->suspendLogForReplay(true);

    if (_logFile.atEnd()) {
        _resetPlaybackToBeginning();
        _pendingUploadItems.clear();
        _pendingUploadCount.clear();
        emit replaySeekMissionResolved({});
    }

    _playbackStartTimeMSecs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    _playbackStartLogTimeUSecs = _logCurrentTimeUSecs;
    _readTickTimer->start(1);

    emit playbackStarted();
}

void LogReplayWorker::pause()
{
    LinkManager::instance()->setConnectionsAllowed();
    MAVLinkProtocol::instance()->suspendLogForReplay(false);

    _readTickTimer->stop();

    emit playbackPaused();
}

void LogReplayWorker::beginStream()
{
    LinkManager::instance()->setConnectionsSuspended(tr("Connect not allowed during Flight Data replay."));
    MAVLinkProtocol::instance()->suspendLogForReplay(true);

    _playbackStartTimeMSecs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    _playbackStartLogTimeUSecs = _logCurrentTimeUSecs;
    _readTickTimer->start(1);

    emit playbackStarted();
}

void LogReplayWorker::_readUntilHeartbeat()
{
    while (!_logFile.atEnd()) {
        QByteArray bytes;
        mavlink_message_t msg{};
        const qint64 nextTimeUSecs = _readNextMavlinkMessage(bytes, msg);
        emit dataReceived(bytes);
        emit playbackPercentCompleteChanged(0.0f);

        if (_logFile.atEnd()) {
            pause();
            emit playbackAtEnd();
            return;
        }

        _logCurrentTimeUSecs = nextTimeUSecs;

        if (msg.msgid == MAVLINK_MSG_ID_HEARTBEAT && msg.compid == MAV_COMP_ID_AUTOPILOT1) {
            break;
        }
    }
}

void LogReplayWorker::setPlaybackSpeed(qreal playbackSpeed)
{
    _playbackSpeed = playbackSpeed;
    _playbackStartTimeMSecs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    _playbackStartLogTimeUSecs = _logCurrentTimeUSecs;
    if (_readTickTimer->isActive())
        _readTickTimer->start(1);
    emit playbackSpeedChanged(playbackSpeed);
}

void LogReplayWorker::movePlayhead(qreal percentComplete)
{
    if (isPlaying()) {
        pause();
        if (_readTickTimer->isActive()) {
            return;
        }
    }

    _pendingUploadItems.clear();
    _pendingUploadCount.clear();

    percentComplete = qBound(0., percentComplete, 100.);
    const qreal percentCompleteMult = percentComplete / 100.0;
    const qint64 newFilePos = static_cast<qint64>(percentCompleteMult * static_cast<qreal>(_logFile.size()));
    if (!_logFile.seek(newFilePos)) {
        emit errorOccurred(tr("Unable to seek to new position"));
        return;
    }

    mavlink_message_t dummy{};
    _logCurrentTimeUSecs = _seekToNextMavlinkMessage(dummy);

    qreal newRelativeTimeUSecs = static_cast<qreal>(_logCurrentTimeUSecs - _logStartTimeUSecs);
    const qreal baudRate = _logFile.size() / static_cast<qreal>(_logDurationUSecs) / 1e6;
    const qreal desiredTimeUSecs = percentCompleteMult * _logDurationUSecs;
    const qint64 offset = (newRelativeTimeUSecs - desiredTimeUSecs) * baudRate;
    if (!_logFile.seek(_logFile.pos() + offset)) {
        emit errorOccurred(tr("Unable to seek to new position"));
        return;
    }

    _logCurrentTimeUSecs = _seekToNextMavlinkMessage(dummy);
    _signalCurrentLogTimeSecs();

    newRelativeTimeUSecs = static_cast<qreal>(_logCurrentTimeUSecs - _logStartTimeUSecs);
    percentComplete = ((newRelativeTimeUSecs / _logDurationUSecs) * 100);
    emit playbackPercentCompleteChanged(percentComplete);

    const quint64 targetTimeUSecs = _logCurrentTimeUSecs;
    const qint64 targetFilePos = _logFile.pos();

    emit seekStarted();

    if (!_logFile.reset()) {
        qCWarning(LogReplayLinkLog) << "Failed to reset log file for seek replay";
        return;
    }
    mavlink_reset_channel_status(_mavlinkChannel);

    QList<QGeoCoordinate> coords;
    QByteArray lastHeartbeatBytes;
    QByteArray lastPositionBytes;
    QByteArray lastAttitudeBytes;
    QByteArray lastMissionCurrentBytes;
    QByteArray lastVfrHudBytes;
    QByteArray lastHomePositionBytes;

    quint64 currentMsgTimeUSecs     = _logStartTimeUSecs;
    quint64 lastArmedTransitionUSecs = 0;
    bool    prevArmedState           = false;
    bool    isArmedAtSeekPoint       = false;
    QGeoCoordinate lastDistCoord;
    double flightDistanceMeters   = 0.0;
    static constexpr double kDistTolerance = 2.0;

    while (!_logFile.atEnd()) {
        QByteArray bytes;
        mavlink_message_t msg{};
        const quint64 nextTimeUSecs = _readNextMavlinkMessage(bytes, msg);
        if (msg.msgid == MAVLINK_MSG_ID_GLOBAL_POSITION_INT) {
            mavlink_global_position_int_t pos;
            mavlink_msg_global_position_int_decode(&msg, &pos);
            const QGeoCoordinate coord(pos.lat / 1e7, pos.lon / 1e7, pos.alt / 1000.0);
            coords.append(coord);
            if (lastDistCoord.isValid()) {
                const double d = lastDistCoord.distanceTo(coord);
                if (d > kDistTolerance) {
                    flightDistanceMeters += d;
                    lastDistCoord = coord;
                }
            } else {
                lastDistCoord = coord;
            }
            lastPositionBytes = bytes;
        } else if (msg.msgid == MAVLINK_MSG_ID_HEARTBEAT && msg.compid == MAV_COMP_ID_AUTOPILOT1) {
            mavlink_heartbeat_t hb;
            mavlink_msg_heartbeat_decode(&msg, &hb);
            const bool armed = (hb.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0;
            if (armed && !prevArmedState) {
                lastArmedTransitionUSecs = currentMsgTimeUSecs;
            }
            prevArmedState     = armed;
            isArmedAtSeekPoint = armed;
            lastHeartbeatBytes = bytes;
        } else if (msg.msgid == MAVLINK_MSG_ID_ATTITUDE) {
            lastAttitudeBytes = bytes;
        } else if (msg.msgid == MAVLINK_MSG_ID_MISSION_CURRENT) {
            lastMissionCurrentBytes = bytes;
        } else if (msg.msgid == MAVLINK_MSG_ID_VFR_HUD) {
            lastVfrHudBytes = bytes;
        } else if (msg.msgid == MAVLINK_MSG_ID_HOME_POSITION) {
            lastHomePositionBytes = bytes;
        }
        currentMsgTimeUSecs = nextTimeUSecs;
        if (nextTimeUSecs == 0 || nextTimeUSecs >= targetTimeUSecs) {
            break;
        }
    }

    const double flightTimeSecs = (isArmedAtSeekPoint && lastArmedTransitionUSecs > 0)
        ? static_cast<double>(targetTimeUSecs - lastArmedTransitionUSecs) / 1e6
        : 0.0;

    emit seekReplayComplete(coords);

    if (!lastHeartbeatBytes.isEmpty()) {
        emit dataReceived(lastHeartbeatBytes);
    }
    if (!lastAttitudeBytes.isEmpty()) {
        emit dataReceived(lastAttitudeBytes);
    }
    if (!lastMissionCurrentBytes.isEmpty()) {
        emit dataReceived(lastMissionCurrentBytes);
    }
    if (!lastHomePositionBytes.isEmpty()) {
        emit dataReceived(lastHomePositionBytes);
    }
    if (!lastVfrHudBytes.isEmpty()) {
        emit dataReceived(lastVfrHudBytes);
    }
    if (!lastPositionBytes.isEmpty()) {
        emit dataReceived(lastPositionBytes);
    }
    emit seekFlightStatsReady(flightTimeSecs, flightDistanceMeters);

    QMap<int, QList<mavlink_mission_item_int_t>> resolved;
    for (auto it = _missionTimeline.constBegin(); it != _missionTimeline.constEnd(); ++it) {
        const auto& snapshots = it.value();
        auto ub = std::upper_bound(snapshots.begin(), snapshots.end(), targetTimeUSecs,
            [](quint64 t, const MissionSnapshot& s) { return t < s.timeUSecs; });
        if (ub != snapshots.begin()) {
            --ub;
            resolved[static_cast<int>(it.key())] = ub->items;
        }
    }
    emit replaySeekMissionResolved(resolved);

    mavlink_reset_channel_status(_mavlinkChannel);
    if (!_logFile.seek(targetFilePos)) {
        qCWarning(LogReplayLinkLog) << "Failed to restore file position after seek replay";
    }
    _logCurrentTimeUSecs = targetTimeUSecs;
}

void LogReplayWorker::_resetPlaybackToBeginning()
{
    if (_logFile.isOpen()) {
        if (!_logFile.reset()) {
            qCWarning(LogReplayLinkLog) << "failed to reset log file:" << _logFile.error() << _logFile.errorString();
        }
    }

    _playbackStartTimeMSecs = 0;
    _playbackStartLogTimeUSecs = 0;
    _logCurrentTimeUSecs = _logStartTimeUSecs;
}

void LogReplayWorker::_buildMissionTimeline()
{
    _missionTimeline.clear();

    if (!_logFile.reset()) return;
    mavlink_reset_channel_status(_mavlinkChannel);

    QMap<uint8_t, QList<mavlink_mission_item_int_t>> pending;
    QMap<uint8_t, uint16_t>                          pendingCount;

    while (!_logFile.atEnd()) {
        QByteArray bytes;
        mavlink_message_t msg{};
        const quint64 timeUSecs = _readNextMavlinkMessage(bytes, msg);
        if (timeUSecs == 0) break;
        if (msg.compid == MAV_COMP_ID_AUTOPILOT1) continue;

        switch (msg.msgid) {
        case MAVLINK_MSG_ID_MISSION_COUNT: {
            mavlink_mission_count_t mc{};
            mavlink_msg_mission_count_decode(&msg, &mc);
            const uint8_t type = mc.mission_type;
            pending.remove(type);
            pendingCount.remove(type);
            if (mc.count == 0) {
                _missionTimeline[type].append(MissionSnapshot{timeUSecs, {}});
            } else {
                pendingCount[type] = mc.count;
            }
            break;
        }
        case MAVLINK_MSG_ID_MISSION_ITEM_INT: {
            mavlink_mission_item_int_t item{};
            mavlink_msg_mission_item_int_decode(&msg, &item);
            const uint8_t type = item.mission_type;
            if (pendingCount.contains(type)) {
                pending[type].append(item);
                if (pending[type].count() == static_cast<int>(pendingCount[type])) {
                    _missionTimeline[type].append(MissionSnapshot{timeUSecs, pending.take(type)});
                    pendingCount.remove(type);
                }
            }
            break;
        }
        case MAVLINK_MSG_ID_MISSION_CLEAR_ALL: {
            mavlink_mission_clear_all_t clear{};
            mavlink_msg_mission_clear_all_decode(&msg, &clear);
            if (clear.mission_type == MAV_MISSION_TYPE_ALL) {
                _missionTimeline[MAV_MISSION_TYPE_MISSION].append(MissionSnapshot{timeUSecs, {}});
                _missionTimeline[MAV_MISSION_TYPE_FENCE].append(MissionSnapshot{timeUSecs, {}});
                _missionTimeline[MAV_MISSION_TYPE_RALLY].append(MissionSnapshot{timeUSecs, {}});
            } else {
                _missionTimeline[clear.mission_type].append(MissionSnapshot{timeUSecs, {}});
            }
            pending.remove(clear.mission_type);
            pendingCount.remove(clear.mission_type);
            break;
        }
        default:
            break;
        }
    }

    if (!_logFile.reset()) {
        qCWarning(LogReplayLinkLog) << "Failed to reset log file after building mission timeline";
    }
    mavlink_reset_channel_status(_mavlinkChannel);
}

void LogReplayWorker::_detectReplayMissionUpload(const mavlink_message_t& msg)
{
    switch (msg.msgid) {
    case MAVLINK_MSG_ID_MISSION_COUNT: {
        mavlink_mission_count_t mc{};
        mavlink_msg_mission_count_decode(&msg, &mc);
        qCDebug(LogReplayLinkLog) << "MISSION_COUNT sysid=" << msg.sysid << "compid=" << msg.compid
                                  << "mission_type=" << mc.mission_type << "count=" << mc.count;
        const uint8_t type = mc.mission_type;
        if (msg.compid != MAV_COMP_ID_AUTOPILOT1 && mc.count > 0) {
            _pendingUploadItems[type].clear();
            _pendingUploadCount[type] = mc.count;
        } else if (msg.compid != MAV_COMP_ID_AUTOPILOT1 && mc.count == 0) {
            emit replayMissionUploaded(type, {});
        }
        break;
    }
    case MAVLINK_MSG_ID_MISSION_ITEM_INT: {
        mavlink_mission_item_int_t item{};
        mavlink_msg_mission_item_int_decode(&msg, &item);
        qCDebug(LogReplayLinkLog) << "MISSION_ITEM_INT sysid=" << msg.sysid << "compid=" << msg.compid
                                  << "mission_type=" << item.mission_type << "seq=" << item.seq;
        const uint8_t type = item.mission_type;
        if (msg.compid != MAV_COMP_ID_AUTOPILOT1 && _pendingUploadCount.contains(type)) {
            _pendingUploadItems[type].append(item);
            const int buffered = _pendingUploadItems[type].count();
            const int expected = static_cast<int>(_pendingUploadCount[type]);
            if (buffered == expected) {
                qCDebug(LogReplayLinkLog) << "replayMissionUploaded type=" << type << "count=" << buffered;
                emit replayMissionUploaded(type, _pendingUploadItems[type]);
                _pendingUploadItems.remove(type);
                _pendingUploadCount.remove(type);
            }
        }
        break;
    }
    case MAVLINK_MSG_ID_MISSION_CLEAR_ALL: {
        mavlink_mission_clear_all_t clear{};
        mavlink_msg_mission_clear_all_decode(&msg, &clear);
        if (msg.compid == MAV_COMP_ID_AUTOPILOT1) break;
        qCDebug(LogReplayLinkLog) << "MISSION_CLEAR_ALL sysid=" << msg.sysid
                                  << "compid=" << msg.compid
                                  << "mission_type=" << clear.mission_type;
        const auto emitClear = [this](uint8_t type) {
            _pendingUploadItems.remove(type);
            _pendingUploadCount.remove(type);
            emit replayMissionUploaded(static_cast<int>(type), {});
        };
        if (clear.mission_type == MAV_MISSION_TYPE_ALL) {
            emitClear(MAV_MISSION_TYPE_MISSION);
            emitClear(MAV_MISSION_TYPE_FENCE);
            emitClear(MAV_MISSION_TYPE_RALLY);
        } else {
            emitClear(clear.mission_type);
        }
        break;
    }
    case MAVLINK_MSG_ID_MISSION_ACK: {
        mavlink_mission_ack_t ack{};
        mavlink_msg_mission_ack_decode(&msg, &ack);
        qCDebug(LogReplayLinkLog) << "MISSION_ACK sysid=" << msg.sysid << "compid=" << msg.compid
                                  << "mission_type=" << ack.mission_type << "result=" << ack.type;
        break;
    }
    case MAVLINK_MSG_ID_MISSION_REQUEST_INT: {
        mavlink_mission_request_int_t req{};
        mavlink_msg_mission_request_int_decode(&msg, &req);
        qCDebug(LogReplayLinkLog) << "MISSION_REQUEST_INT sysid=" << msg.sysid << "compid=" << msg.compid
                                  << "mission_type=" << req.mission_type << "seq=" << req.seq;
        break;
    }
    default:
        break;
    }
}

void LogReplayWorker::_readNextLogEntry()
{
    int timeToNextExecutionMSecs = 0;
    while (timeToNextExecutionMSecs < 3) {
        QByteArray bytes;
        bytes.reserve(_logFile.bytesAvailable());
        mavlink_message_t msg{};
        const qint64 nextTimeUSecs = _readNextMavlinkMessage(bytes, msg);
        _detectReplayMissionUpload(msg);
        emit dataReceived(bytes);
        emit playbackPercentCompleteChanged((static_cast<float>(_logCurrentTimeUSecs - _logStartTimeUSecs) / static_cast<float>(_logDurationUSecs)) * 100);

        if (_logFile.atEnd()) {
            pause();
            emit playbackAtEnd();
            return;
        }

        _logCurrentTimeUSecs = nextTimeUSecs;

        const quint64 currentTimeMSecs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
        const quint64 desiredPlayheadMovementTimeMSecs = ((_logCurrentTimeUSecs - _playbackStartLogTimeUSecs) / 1000) / _playbackSpeed;
        const quint64 desiredCurrentTimeMSecs = _playbackStartTimeMSecs + desiredPlayheadMovementTimeMSecs;
        timeToNextExecutionMSecs = desiredCurrentTimeMSecs - currentTimeMSecs;
    }

    _signalCurrentLogTimeSecs();

    _readTickTimer->start(timeToNextExecutionMSecs);
}

void LogReplayWorker::_signalCurrentLogTimeSecs()
{
    emit currentLogTimeSecs((_logCurrentTimeUSecs - _logStartTimeUSecs) / 1000000);
}

bool LogReplayWorker::_loadLogFile()
{
    if (_logFile.isOpen()) {
        _logFile.close();
        emit errorOccurred(tr("Attempt to load new log while log being played"));
        return false;
    }

    const QString logFilename = _logReplayConfig->logFilename();
    _logFile.setFileName(logFilename);
    if (!_logFile.open(QFile::ReadOnly)) {
        emit errorOccurred(tr("Unable to open log file: '%1', error: %2").arg(logFilename, _logFile.errorString()));
        return false;
    }

    QFileInfo logFileInfo;
    logFileInfo.setFile(logFilename);
    _logFileSize = logFileInfo.size();

    const quint64 startTimeUSecs = _parseTimestamp(_logFile.read(kTimestamp));
    const quint64 endTimeUSecs = _findLastTimestamp();
    if (endTimeUSecs <= startTimeUSecs) {
        _logFile.close();
        emit errorOccurred(tr("The log file '%1' is corrupt or empty.").arg(logFilename));
        return false;
    }

    _logEndTimeUSecs = endTimeUSecs;
    _logStartTimeUSecs = startTimeUSecs;
    _logDurationUSecs = endTimeUSecs - startTimeUSecs;
    _logCurrentTimeUSecs = startTimeUSecs;

    if (!_logFile.reset()) {
        qCWarning(LogReplayLinkLog) << "failed to reset log file:" << _logFile.error() << _logFile.errorString();
    }

    const quint64 logDurationSecondsTotal = _logDurationUSecs / 1000000;
    emit logFileStats(logDurationSecondsTotal);

    _buildMissionTimeline();

    return true;
}

quint64 LogReplayWorker::_parseTimestamp(const QByteArray &bytes)
{
    const quint64 currentTimestamp = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
    quint64 timestamp = qFromBigEndian(*reinterpret_cast<const quint64*>(bytes.constData()));
    if (timestamp > currentTimestamp) {
        timestamp = qbswap(timestamp);
    }

    return timestamp;
}

quint64 LogReplayWorker::_readNextMavlinkMessage(QByteArray &bytes)
{
    mavlink_message_t dummy{};
    return _readNextMavlinkMessage(bytes, dummy);
}

quint64 LogReplayWorker::_readNextMavlinkMessage(QByteArray &bytes, mavlink_message_t &outMsg)
{
    bytes.clear();

    char nextByte;
    while (_logFile.getChar(&nextByte)) {
        mavlink_message_t message{};
        mavlink_status_t status{};
        const bool messageFound = mavlink_parse_char(_mavlinkChannel, nextByte, &message, &status);

        if (status.parse_state == MAVLINK_PARSE_STATE_GOT_STX) {
            bytes.clear();
        }
        (void) bytes.append(nextByte);

        if (messageFound) {
            outMsg = message;
            const QByteArray rawTime = _logFile.read(kTimestamp);
            return _parseTimestamp(rawTime);
        }
    }

    return 0;
}

quint64 LogReplayWorker::_seekToNextMavlinkMessage(mavlink_message_t &nextMsg)
{
    mavlink_reset_channel_status(_mavlinkChannel);

    qint64 messageStartPos = -1;
    char nextByte;
    while (_logFile.getChar(&nextByte)) {
        mavlink_status_t status{};
        const bool messageFound = mavlink_parse_char(_mavlinkChannel, nextByte, &nextMsg, &status);

        if (status.parse_state == MAVLINK_PARSE_STATE_GOT_STX) {
            messageStartPos = _logFile.pos() - 1;
        }

        if (messageFound && (messageStartPos != -1)) {
            if (!_logFile.seek(messageStartPos - kTimestamp)) {
                qCWarning(LogReplayLinkLog) << "Failed to seek next message:" << _logFile.error() << _logFile.errorString();
                break;
            }

            const QByteArray rawTime = _logFile.read(kTimestamp);
            return _parseTimestamp(rawTime);
        }
    }

    return 0;
}

quint64 LogReplayWorker::_findLastTimestamp()
{
    if (!_logFile.reset()) {
        qCWarning(LogReplayLinkLog) << "failed to reset log file:" << _logFile.error() << _logFile.errorString();
    }

    mavlink_reset_channel_status(_mavlinkChannel);

    quint64 lastTimestamp = 0;

    while (_logFile.bytesAvailable() > static_cast<qint64>(kTimestamp)) {
        lastTimestamp = _parseTimestamp(_logFile.read(kTimestamp));

        bool endOfMessage = false;
        char nextByte;
        while (!endOfMessage && _logFile.getChar(&nextByte)) {
            mavlink_message_t msg{};
            mavlink_status_t status{};
            endOfMessage = mavlink_parse_char(_mavlinkChannel, nextByte, &msg, &status);
        }
    }

    return lastTimestamp;
}

/*===========================================================================*/

LogReplayLink::LogReplayLink(SharedLinkConfigurationPtr &config, QObject *parent)
    : LinkInterface(config, parent)
    , _logReplayConfig(qobject_cast<LogReplayConfiguration*>(config.get()))
    , _worker(new LogReplayWorker(_logReplayConfig))
    , _workerThread(new QThread(this))
{
    qCDebug(LogReplayLinkLog) << this;

    _workerThread->setObjectName(QStringLiteral("LogReplay_%1").arg(_logReplayConfig->name()));

    _worker->moveToThread(_workerThread);

    (void) connect(_workerThread, &QThread::started, _worker, &LogReplayWorker::setup);
    (void) connect(_workerThread, &QThread::finished, _worker, &QObject::deleteLater);

    (void) connect(_worker, &LogReplayWorker::connected, this, &LogReplayLink::_onConnected, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::disconnected, this, &LogReplayLink::_onDisconnected, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::errorOccurred, this, &LogReplayLink::_onErrorOccurred, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::dataReceived, this, &LogReplayLink::_onDataReceived, Qt::QueuedConnection);

    (void) connect(_worker, &LogReplayWorker::logFileStats, this, &LogReplayLink::logFileStats, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::playbackStarted, this, &LogReplayLink::playbackStarted, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::playbackPaused, this, &LogReplayLink::playbackPaused, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::playbackPercentCompleteChanged, this, &LogReplayLink::playbackPercentCompleteChanged, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::currentLogTimeSecs, this, &LogReplayLink::currentLogTimeSecs, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::seekStarted, this, &LogReplayLink::seekStarted, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::seekReplayComplete,    this, &LogReplayLink::seekReplayComplete,    Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::seekFlightStatsReady,  this, &LogReplayLink::seekFlightStatsReady,  Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::playbackSpeedChanged, this, &LogReplayLink::playbackSpeedChanged, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::disconnected, this, &LogReplayLink::disconnected, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::replayMissionUploaded, this, &LogReplayLink::replayMissionUploaded, Qt::QueuedConnection);
    (void) connect(_worker, &LogReplayWorker::replaySeekMissionResolved, this, &LogReplayLink::replaySeekMissionResolved, Qt::QueuedConnection);

    _workerThread->start();
}

LogReplayLink::~LogReplayLink()
{
    if (isConnected()) {
        (void) QMetaObject::invokeMethod(_worker, "disconnectFromLog", Qt::BlockingQueuedConnection);
        _onDisconnected();
    }

    _workerThread->quit();
    if (!_workerThread->wait()) {
        qCWarning(LogReplayLinkLog) << "Failed to wait for LogReplay Thread to close";
    }

    qCDebug(LogReplayLinkLog) << this;
}

bool LogReplayLink::isConnected() const
{
    return _worker && _worker->isConnected();
}

bool LogReplayLink::_connect()
{
    return QMetaObject::invokeMethod(_worker, "connectToLog", Qt::QueuedConnection);
}

void LogReplayLink::disconnect()
{
    if (isConnected()) {
        (void) QMetaObject::invokeMethod(_worker, "disconnectFromLog", Qt::QueuedConnection);
    }
}

void LogReplayLink::_onConnected()
{
    _disconnectedEmitted = false;
    emit connected();
}

void LogReplayLink::_onDisconnected()
{
    if (!_disconnectedEmitted.exchange(true)) {
        emit disconnected();
    }
}

void LogReplayLink::_onErrorOccurred(const QString &errorString)
{
    qCWarning(LogReplayLinkLog) << "Error:" << errorString;
    emit communicationError(tr("Log Replay Link Error"), tr("Link: %1, %2.").arg(_logReplayConfig->name(), errorString));
}

void LogReplayLink::_onDataReceived(const QByteArray &data)
{
    emit bytesReceived(this, data);
}

bool LogReplayLink::isPlaying() const
{
    return _worker && _worker->isPlaying();
}

void LogReplayLink::play()
{
    (void) QMetaObject::invokeMethod(_worker, "play", Qt::QueuedConnection);
}

void LogReplayLink::pause()
{
    (void) QMetaObject::invokeMethod(_worker, "pause", Qt::QueuedConnection);
}

void LogReplayLink::beginStream()
{
    (void) QMetaObject::invokeMethod(_worker, "beginStream", Qt::QueuedConnection);
}

void LogReplayLink::setPlaybackSpeed(qreal playbackSpeed)
{
    (void) QMetaObject::invokeMethod(_worker, "setPlaybackSpeed", Qt::QueuedConnection, playbackSpeed);
}

void LogReplayLink::movePlayhead(qreal percentComplete)
{
    (void) QMetaObject::invokeMethod(_worker, "movePlayhead", Qt::QueuedConnection, percentComplete);
}

void LogReplayLink::requestPlanReload()
{
    emit replayPlanReloadRequested();
}
