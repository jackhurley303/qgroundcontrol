#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QSet>
#include <QtCore/QString>

#include "LinkInterface.h"
#include "MAVLinkEnums.h"
#include "MAVLinkMessageType.h"

class QFile;

/// \brief MAVLink micro air vehicle protocol reference implementation.
///
class MAVLinkProtocol : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    tlogLogging           READ tlogLogging           NOTIFY tlogLoggingChanged)
    Q_PROPERTY(bool    hasPendingLog         READ hasPendingLog         NOTIFY hasPendingLogChanged)
    Q_PROPERTY(QString pendingLogName        READ pendingLogName        NOTIFY hasPendingLogChanged)
    Q_PROPERTY(int     autoCompletedLogCount READ autoCompletedLogCount NOTIFY autoCompletedLogCountChanged)

public:
    explicit MAVLinkProtocol(QObject* parent = nullptr);

    ~MAVLinkProtocol();

    static MAVLinkProtocol* instance();

    void init();

    static QString getName() { return QStringLiteral("MAVLink protocol"); }

    int getSystemId() const;

    static int getComponentId() { return MAV_COMP_ID_MISSIONPLANNER; }

    void resetMetadataForLink(LinkInterface* link);

    /// Reset sequence tracking so signing transitions don't inflate loss counters.
    void resetSequenceTracking(LinkInterface* link);

    void suspendLogForReplay(bool suspend) { _logSuspendReplay = suspend; }

    void checkForLostLogFiles();

    /// Returns true when a tlog is actively being written.
    bool tlogLogging() const;

    /// Returns true when a completed tlog is waiting for a save/discard decision.
    bool hasPendingLog() const;

    /// The filename (no directory) the pending log will be saved as, e.g. "2024-01-15 14-30-00.tlog".
    /// Empty when hasPendingLog is false.
    QString pendingLogName() const;

    /// Count of recordings that were stopped automatically (e.g. vehicle disconnect)
    /// since clearAutoCompletedLogCount() was last called. Does not increment when
    /// the user stops logging manually via stopTlogLogging().
    int autoCompletedLogCount() const { return _autoCompletedLogCount; }

    /// Reset the auto-completed log count to zero (call when the panel becomes visible).
    Q_INVOKABLE void clearAutoCompletedLogCount();

    /// Manually start tlog recording. No-op if already logging or a pending log exists.
    Q_INVOKABLE void startTlogLogging();

    /// Manually stop tlog recording. The completed log becomes a pending log.
    Q_INVOKABLE void stopTlogLogging();

    /// Save the pending tlog to the permanent telemetry directory.
    Q_INVOKABLE void savePendingLog();

    /// Discard (delete) the pending tlog without saving.
    Q_INVOKABLE void discardPendingLog();

signals:
    void vehicleHeartbeatInfo(LinkInterface* link, int vehicleId, int componentId, int vehicleFirmwareType,
                              int vehicleType);

    void messageReceived(LinkInterface* link, const mavlink_message_t& message);

    void mavlinkMessageStatus(int sysid, uint64_t totalSent, uint64_t totalReceived, uint64_t totalLoss,
                              float lossPercent);

    void tlogLoggingChanged();
    void hasPendingLogChanged();
    void autoCompletedLogCountChanged();

public slots:
    void receiveBytes(LinkInterface* link, const QByteArray& data);

    void logSentBytes(const LinkInterface* link, const QByteArray& data);

    static void deleteTempLogFiles();

private slots:
    void _vehicleCountChanged();

private:
    void _logData(LinkInterface* link, const mavlink_message_t& message);
    bool _closeLogFile();
    void _startLogging();
    void _stopLogging();

    void _forward(const mavlink_message_t& message);
    void _forwardSupport(const mavlink_message_t& message);

    void _updateCounters(uint8_t mavlinkChannel, const mavlink_message_t& message);
    bool _updateStatus(LinkInterface* link, const SharedLinkInterfacePtr linkPtr, uint8_t mavlinkChannel,
                       const mavlink_message_t& message);

    void _saveTelemetryLog(const QString &tempLogfile, const QString &desiredFileName = QString());
    bool _checkTelemetrySavePath();

    QFile   *_tempLogFile = nullptr;
    QString  _pendingLogPath;       ///< Path to a completed-but-not-yet-saved temp log
    QString  _pendingLogName;       ///< Intended save filename (e.g. "2024-01-15 14-30-00.tlog")

    bool _logSuspendError  = false; ///< true: Logging suspended due to error
    bool _logSuspendReplay = false; ///< true: Logging suspended due to replay
    bool _vehicleWasArmed  = false; ///< true: Vehicle was armed during log sequence
    bool _manualStop       = false; ///< true: stopTlogLogging() is in progress (don't increment auto-count)
    int  _autoCompletedLogCount = 0;///< Recordings ended by something other than the user pressing Stop

    /// Per-(channel, sysid, compid) last sequence ID. Channel-scoped so traffic on link A doesn't perturb expected
    /// sequence on link B (which has independent sequence histories from the same vehicle).
    uint8_t _lastIndex[MAVLINK_COMM_NUM_BUFFERS][256][256]{};

    QSet<QPair<uint8_t, uint8_t>> _firstMessageSeen[MAVLINK_COMM_NUM_BUFFERS];
    uint64_t _totalReceiveCounter[MAVLINK_COMM_NUM_BUFFERS]{};
    uint64_t _totalLossCounter[MAVLINK_COMM_NUM_BUFFERS]{};
    float _runningLossPercent[MAVLINK_COMM_NUM_BUFFERS]{};

    bool _initialized = false;

    static constexpr const char* _tempLogFileTemplate = "FlightDataXXXXXX";
    static constexpr const char* _logFileExtension = "mavlink";

    static constexpr uint8_t kMaxCompId = MAV_COMPONENT_ENUM_END - 1;
};
