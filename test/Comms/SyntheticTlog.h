#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QString>

#include "MAVLinkLib.h"

/// Builds synthetic .tlog byte streams with a known event schedule, so a seek to a known
/// log time has a known expected mission and parameter state.
///
/// Events may be added in any order; they are sorted by timestamp when the stream is
/// written. Times are in seconds relative to the start of the log.
class SyntheticTlog
{
public:
    static constexpr quint64 kBaseTimeUSecs = 1700000000000000ULL;
    static constexpr uint8_t kSysId = 1;
    /// Mission uploads come from a ground station, never from the autopilot - the timeline
    /// ignores autopilot-sourced mission messages because those are only the readback.
    static constexpr uint8_t kGcsCompId = MAV_COMP_ID_MISSIONPLANNER;

    explicit SyntheticTlog(quint64 baseTimeUSecs = kBaseTimeUSecs);

    /// HEARTBEAT + GLOBAL_POSITION_INT at each whole second in [fromSecs, toSecs).
    SyntheticTlog &flight(int fromSecs, int toSecs);
    /// cMessages HEARTBEATs all sharing one timestamp, as a real log carries whenever
    /// several frames arrive in a single link read - timestamps are stamped per read, at
    /// millisecond resolution, so runs of identical timestamps are the norm rather than an
    /// edge case.
    SyntheticTlog &heartbeatBurst(double atSecs, int cMessages);
    SyntheticTlog &paramValue(double atSecs, const QString &paramId, float value,
                              uint8_t paramType = MAV_PARAM_TYPE_REAL32);
    /// A complete GCS mission upload: MISSION_COUNT followed by itemCount MISSION_ITEM_INTs.
    SyntheticTlog &missionUpload(double atSecs, int itemCount,
                                 uint8_t missionType = MAV_MISSION_TYPE_MISSION);

    QByteArray bytes() const;

    /// The canonical replay fixture: a 30 second flight, a t=0.1 parameter flood which
    /// repeats PARAM_A at t=0.2 (the timeline must filter the duplicate), PARAM_A changing
    /// at t=10.5 and t=20.5, PARAM_C first observed at t=20.5 (so earlier seek targets must
    /// resolve it to resetToInitial), and GCS mission uploads of 2 items at t=5.5 and 3
    /// items at t=15.5.
    static QByteArray canonicalFlight();

    /// Log time of each canonicalFlight() event, for tests asserting either side of one.
    static constexpr double kParamFloodSecs = 0.1;
    static constexpr double kMissionUpload1Secs = 5.5;
    static constexpr double kParamAChange1Secs = 10.5;
    static constexpr double kMissionUpload2Secs = 15.5;
    static constexpr double kParamAChange2Secs = 20.5;
    static constexpr int kFlightDurationSecs = 30;

private:
    void _append(double atSecs, const mavlink_message_t &msg);

    quint64 _baseTimeUSecs;
    QList<QPair<quint64, QByteArray>> _events;
};
