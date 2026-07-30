/****************************************************************************
 *
 * (c) 2009-2025 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "ReplaySeekApplier.h"

#include "FactSystem/ParameterManager.h"
#include "MissionManager/GeoFenceManager.h"
#include "MissionManager/MissionManager.h"
#include "MissionManager/RallyPointManager.h"
#include "QGCLoggingCategory.h"
#include "Vehicle/MultiVehicleManager.h"
#include "Vehicle/Vehicle.h"

#include <QtCore/QVariant>

QGC_LOGGING_CATEGORY(ReplaySeekApplierLog, "Comms.ReplaySeekApplier")

ReplaySeekApplier::ReplaySeekApplier(LogReplayLink* link)
    : QObject(link)
    , _link(link)
{
    (void) connect(_link, &LogReplayLink::replayMissionUploaded,
                    this, &ReplaySeekApplier::_onReplayMissionUploaded);
    (void) connect(_link, &LogReplayLink::replaySeekMissionResolved,
                    this, &ReplaySeekApplier::_onSeekMissionResolved);
    (void) connect(_link, &LogReplayLink::replaySeekParamResolved,
                    this, &ReplaySeekApplier::_onSeekParamResolved);
}

void ReplaySeekApplier::_onReplayMissionUploaded(int missionType, QList<mavlink_mission_item_int_t> rawItems)
{
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle) return;

    switch (static_cast<MAV_MISSION_TYPE>(missionType)) {
    case MAV_MISSION_TYPE_MISSION: vehicle->missionManager()->loadItemsFromReplay(rawItems); break;
    case MAV_MISSION_TYPE_FENCE:   vehicle->geoFenceManager()->loadItemsFromReplay(rawItems); break;
    case MAV_MISSION_TYPE_RALLY:   vehicle->rallyPointManager()->loadItemsFromReplay(rawItems); break;
    default:
        qCWarning(ReplaySeekApplierLog) << "_onReplayMissionUploaded: unknown missionType=" << missionType;
        return;
    }
    _tlogSnapshotAppliedTypes.insert(missionType);
    _planFileAppliedTypes.remove(missionType);
    _lastAppliedByType[missionType] = rawItems;
}

static bool _missionItemEqual(const mavlink_mission_item_int_t& a,
                               const mavlink_mission_item_int_t& b)
{
    return a.command      == b.command
        && a.frame        == b.frame
        && a.param1       == b.param1
        && a.param2       == b.param2
        && a.param3       == b.param3
        && a.param4       == b.param4
        && a.x            == b.x
        && a.y            == b.y
        && a.z            == b.z
        && a.autocontinue == b.autocontinue
        && a.mission_type == b.mission_type;
}

static bool _missionItemListsEqual(const QList<mavlink_mission_item_int_t>& a,
                                    const QList<mavlink_mission_item_int_t>& b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        if (!_missionItemEqual(a[i], b[i])) return false;
    }
    return true;
}

void ReplaySeekApplier::_onSeekMissionResolved(QMap<int, QList<mavlink_mission_item_int_t>> resolvedByType)
{
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle) return;
    const int sysId = vehicle->id();
    const bool hasPlanFile = !Vehicle::peekReplayPlanFile(sysId).isEmpty();

    // Restore initial state for types without a resolved timeline snapshot
    bool needsRestore = false;
    for (int t : {MAV_MISSION_TYPE_MISSION, MAV_MISSION_TYPE_FENCE, MAV_MISSION_TYPE_RALLY}) {
        if (!resolvedByType.contains(t)) {
            needsRestore = true;
            if (!hasPlanFile) {
                // Only reload if not already empty — avoids clearing+rebuilding an already-empty list
                if (!_lastAppliedByType.contains(t) || !_lastAppliedByType[t].isEmpty()) {
                    switch (t) {
                    case MAV_MISSION_TYPE_MISSION: vehicle->missionManager()->loadItemsFromReplay({}); break;
                    case MAV_MISSION_TYPE_FENCE:   vehicle->geoFenceManager()->loadItemsFromReplay({}); break;
                    case MAV_MISSION_TYPE_RALLY:   vehicle->rallyPointManager()->loadItemsFromReplay({}); break;
                    default: break;
                    }
                    _lastAppliedByType[t] = {};
                }
            }
        }
    }
    if (hasPlanFile && needsRestore) {
        // Only reload if at least one restored type is reverting FROM a tlog snapshot back to
        // plan-file state. If no tlog snapshot was ever applied for a type, the plan file is
        // already current from vehicle startup — requestPlanReload is not needed.
        bool needsPlanReload = false;
        for (int t : {MAV_MISSION_TYPE_MISSION, MAV_MISSION_TYPE_FENCE, MAV_MISSION_TYPE_RALLY}) {
            if (!resolvedByType.contains(t)
                    && !_planFileAppliedTypes.contains(t)
                    && _tlogSnapshotAppliedTypes.contains(t)) {
                needsPlanReload = true;
                break;
            }
        }
        if (needsPlanReload) {
            _link->requestPlanReload();
        }
        for (int t : {MAV_MISSION_TYPE_MISSION, MAV_MISSION_TYPE_FENCE, MAV_MISSION_TYPE_RALLY}) {
            if (!resolvedByType.contains(t)) {
                _planFileAppliedTypes.insert(t);
                _lastAppliedByType.remove(t);
            }
        }
    }

    // Apply resolved states (overrides plan file for types that had tlog events).
    // Skip types whose items are unchanged to avoid the clear+rebuild flicker.
    for (auto it = resolvedByType.constBegin(); it != resolvedByType.constEnd(); ++it) {
        const int type = it.key();
        const auto& items = it.value();
        if (_lastAppliedByType.contains(type) && _missionItemListsEqual(_lastAppliedByType[type], items)) {
            continue;
        }
        switch (type) {
        case MAV_MISSION_TYPE_MISSION: vehicle->missionManager()->loadItemsFromReplay(items); break;
        case MAV_MISSION_TYPE_FENCE:   vehicle->geoFenceManager()->loadItemsFromReplay(items); break;
        case MAV_MISSION_TYPE_RALLY:   vehicle->rallyPointManager()->loadItemsFromReplay(items); break;
        default: break;
        }
        _tlogSnapshotAppliedTypes.insert(type);
        _planFileAppliedTypes.remove(type);
        _lastAppliedByType[type] = items;
    }

    // Reset so the next MISSION_CURRENT always fires currentIndexChanged,
    // ensuring the correct waypoint is highlighted after the seek.
    vehicle->missionManager()->resetCurrentIndex();
}

void ReplaySeekApplier::_onSeekParamResolved(int sysId, QList<ParamSeekValue> resolved)
{
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle || vehicle->id() != sysId) return;

    ParameterManager* pm = vehicle->parameterManager();
    for (const ParamSeekValue& sv : resolved) {
        if (sv.resetToInitial) {
            pm->resetParamToReplayInitial(sv.compId, sv.paramId);
        } else {
            // MAVLink encodes integer params by storing their raw bytes in the float
            // param_value field. Read through mavlink_param_union_t — direct static_cast
            // would numerically convert the denormalized float to 0.
            mavlink_param_union_t pu;
            pu.param_float = sv.rawValue;
            QVariant value;
            switch (static_cast<MAV_PARAM_TYPE>(sv.paramType)) {
            case MAV_PARAM_TYPE_REAL32: value = QVariant(pu.param_float);                        break;
            case MAV_PARAM_TYPE_UINT32: value = QVariant(static_cast<quint32>(pu.param_uint32)); break;
            case MAV_PARAM_TYPE_UINT16: value = QVariant(static_cast<quint16>(pu.param_uint16)); break;
            case MAV_PARAM_TYPE_INT16:  value = QVariant(static_cast<qint16>(pu.param_int16));   break;
            case MAV_PARAM_TYPE_UINT8:  value = QVariant(static_cast<quint8>(pu.param_uint8));   break;
            case MAV_PARAM_TYPE_INT8:   value = QVariant(static_cast<qint8>(pu.param_int8));     break;
            default:                    value = QVariant(static_cast<qint32>(pu.param_int32));    break;
            }
            pm->setParamFromReplaySeek(sv.compId, sv.paramId, value);
        }
    }
}
