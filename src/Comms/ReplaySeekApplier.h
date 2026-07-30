/****************************************************************************
 *
 * (c) 2009-2025 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "LogReplayLink.h"
#include "MAVLinkLib.h"

#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QSet>

/**
 * @brief Host-side consumer of a LogReplayLink's typed replay signals.
 *
 * Applies tlog-snapshot mission items and parameter seeks directly to the
 * active vehicle, keyed off host-owned state only (MultiVehicleManager,
 * Vehicle::peekReplayPlanFile(), ParameterManager's cached replay initials).
 * Created as a child of every LogReplayLink at the single creation point in
 * LinkManager::createConnectedLink() so its per-session bookkeeping resets
 * with the link by construction. No SDK surface change: the signals it
 * consumes never cross the plugin boundary.
 */
class ReplaySeekApplier : public QObject
{
    Q_OBJECT

public:
    explicit ReplaySeekApplier(LogReplayLink* link);

private:
    void _onReplayMissionUploaded(int missionType, QList<mavlink_mission_item_int_t> rawItems);
    void _onSeekMissionResolved(QMap<int, QList<mavlink_mission_item_int_t>> resolvedByType);
    void _onSeekParamResolved(int sysId, QList<ParamSeekValue> resolved);

    LogReplayLink* _link = nullptr;

    QMap<int, QList<mavlink_mission_item_int_t>> _lastAppliedByType;     ///< Last tlog-snapshot items applied, keyed by MAV_MISSION_TYPE
    QSet<int>                                    _planFileAppliedTypes;  ///< Types currently showing plan-file state (requestPlanReload was last applied)
    QSet<int>                                    _tlogSnapshotAppliedTypes; ///< Types that have ever had a tlog snapshot applied this session
};
