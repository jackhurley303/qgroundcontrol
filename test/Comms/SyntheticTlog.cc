#include "SyntheticTlog.h"

#include <QtCore/QtEndian>

#include <algorithm>

SyntheticTlog::SyntheticTlog(quint64 baseTimeUSecs)
    : _baseTimeUSecs(baseTimeUSecs)
{
}

void SyntheticTlog::_append(double atSecs, const mavlink_message_t &msg)
{
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN]{};
    const int cBuffer = mavlink_msg_to_send_buffer(buffer, &msg);

    const quint64 timeUSecs = _baseTimeUSecs + static_cast<quint64>(atSecs * 1e6);
    _events.append({timeUSecs, QByteArray(reinterpret_cast<const char*>(buffer), cBuffer)});
}

SyntheticTlog &SyntheticTlog::flight(int fromSecs, int toSecs)
{
    for (int secs = fromSecs; secs < toSecs; secs++) {
        mavlink_message_t heartbeat{};
        (void) mavlink_msg_heartbeat_pack(kSysId, MAV_COMP_ID_AUTOPILOT1, &heartbeat, MAV_TYPE_QUADROTOR,
                                          MAV_AUTOPILOT_PX4, MAV_MODE_FLAG_SAFETY_ARMED, 0, MAV_STATE_ACTIVE);
        _append(secs, heartbeat);

        // A track heading steadily north, so a seek has a non-degenerate trajectory to rebuild.
        mavlink_message_t position{};
        (void) mavlink_msg_global_position_int_pack(kSysId, MAV_COMP_ID_AUTOPILOT1, &position,
                                                    static_cast<uint32_t>(secs * 1000),
                                                    475000000 + (secs * 1000), 85000000, 100000, 50000,
                                                    0, 0, 0, 0);
        _append(secs, position);
    }

    return *this;
}

SyntheticTlog &SyntheticTlog::heartbeatBurst(double atSecs, int cMessages)
{
    for (int i = 0; i < cMessages; i++) {
        mavlink_message_t heartbeat{};
        (void) mavlink_msg_heartbeat_pack(kSysId, MAV_COMP_ID_AUTOPILOT1, &heartbeat, MAV_TYPE_QUADROTOR,
                                          MAV_AUTOPILOT_PX4, MAV_MODE_FLAG_SAFETY_ARMED, 0, MAV_STATE_ACTIVE);
        _append(atSecs, heartbeat);
    }

    return *this;
}

SyntheticTlog &SyntheticTlog::paramValue(double atSecs, const QString &paramId, float value, uint8_t paramType)
{
    const QByteArray paramIdBytes = paramId.toLatin1();

    mavlink_message_t msg{};
    (void) mavlink_msg_param_value_pack(kSysId, MAV_COMP_ID_AUTOPILOT1, &msg, paramIdBytes.constData(),
                                        value, paramType, 1, 0);
    _append(atSecs, msg);

    return *this;
}

SyntheticTlog &SyntheticTlog::missionUpload(double atSecs, int itemCount, uint8_t missionType)
{
    mavlink_message_t count{};
    (void) mavlink_msg_mission_count_pack(kSysId, kGcsCompId, &count, kSysId, MAV_COMP_ID_AUTOPILOT1,
                                          static_cast<uint16_t>(itemCount), missionType, 0);
    _append(atSecs, count);

    for (int i = 0; i < itemCount; i++) {
        mavlink_message_t item{};
        (void) mavlink_msg_mission_item_int_pack(kSysId, kGcsCompId, &item, kSysId, MAV_COMP_ID_AUTOPILOT1,
                                                 static_cast<uint16_t>(i), MAV_FRAME_GLOBAL_RELATIVE_ALT_INT,
                                                 MAV_CMD_NAV_WAYPOINT, 0, 1, 0, 0, 0, 0,
                                                 475000000 + (i * 1000), 85000000, 50, missionType);
        // Spread within the same second so the items follow their count in stream order.
        _append(atSecs + ((i + 1) * 0.001), item);
    }

    return *this;
}

QByteArray SyntheticTlog::bytes() const
{
    QList<QPair<quint64, QByteArray>> ordered = _events;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const QPair<quint64, QByteArray> &a, const QPair<quint64, QByteArray> &b) {
                         return a.first < b.first;
                     });

    QByteArray tlog;
    for (const QPair<quint64, QByteArray> &event : ordered) {
        const quint64 timestampUSecs = qToBigEndian<quint64>(event.first);
        (void) tlog.append(reinterpret_cast<const char*>(&timestampUSecs), sizeof(timestampUSecs));
        (void) tlog.append(event.second);
    }

    return tlog;
}

QByteArray SyntheticTlog::canonicalFlight()
{
    SyntheticTlog tlog;

    (void) tlog.flight(0, kFlightDurationSecs);

    (void) tlog.paramValue(kParamFloodSecs, QStringLiteral("PARAM_A"), 1.0f);
    (void) tlog.paramValue(kParamFloodSecs, QStringLiteral("PARAM_B"), 100.0f);
    // Unchanged repeat of the flood value: must not become a timeline entry of its own.
    (void) tlog.paramValue(0.2, QStringLiteral("PARAM_A"), 1.0f);

    (void) tlog.missionUpload(kMissionUpload1Secs, 2);
    (void) tlog.paramValue(kParamAChange1Secs, QStringLiteral("PARAM_A"), 5.0f);
    (void) tlog.missionUpload(kMissionUpload2Secs, 3);
    (void) tlog.paramValue(kParamAChange2Secs, QStringLiteral("PARAM_A"), 9.0f);
    (void) tlog.paramValue(kParamAChange2Secs, QStringLiteral("PARAM_C"), 7.0f);

    return tlog.bytes();
}
