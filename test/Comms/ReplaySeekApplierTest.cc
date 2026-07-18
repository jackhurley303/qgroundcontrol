#include "ReplaySeekApplierTest.h"

#include "FactSystem/Fact.h"
#include "FactSystem/ParameterManager.h"
#include "LogReplayLink.h"
#include "MAVLinkLib.h"
#include "MissionManager/MissionManager.h"
#include "ReplaySeekApplier.h"
#include "Vehicle/Vehicle.h"

#include <QtTest/QSignalSpy>

namespace {

mavlink_mission_item_int_t _makeWaypoint(int seq)
{
    mavlink_mission_item_int_t item{};
    item.seq = static_cast<uint16_t>(seq);
    item.command = MAV_CMD_NAV_WAYPOINT;
    item.frame = MAV_FRAME_GLOBAL_RELATIVE_ALT_INT;
    item.autocontinue = 1;
    return item;
}

} // namespace

void ReplaySeekApplierTest::_missionUploadAppliesItems_test()
{
    SharedLinkConfigurationPtr config = std::make_shared<LogReplayConfiguration>(QStringLiteral("ReplaySeekApplierTest"));
    // Declaration order matters: applier is a QObject child of link, so link must
    // outlive it here — reversing this order would double-delete applier on unwind.
    LogReplayLink link(config);
    ReplaySeekApplier applier(&link);

    const QList<mavlink_mission_item_int_t> items{_makeWaypoint(0), _makeWaypoint(1)};
    QSignalSpy availableSpy(vehicle()->missionManager(), &MissionManager::newMissionItemsAvailable);

    QVERIFY(QMetaObject::invokeMethod(&link, "replayMissionUploaded",
                                       Q_ARG(int, MAV_MISSION_TYPE_MISSION),
                                       Q_ARG(QList<mavlink_mission_item_int_t>, items)));

    QCOMPARE(availableSpy.count(), 1);
    QCOMPARE(vehicle()->missionManager()->missionItems().count(), items.count());
}

void ReplaySeekApplierTest::_seekMissionResolvedRestoresWithoutPlanFile_test()
{
    SharedLinkConfigurationPtr config = std::make_shared<LogReplayConfiguration>(QStringLiteral("ReplaySeekApplierTest"));
    // Declaration order matters: applier is a QObject child of link, so link must
    // outlive it here — reversing this order would double-delete applier on unwind.
    LogReplayLink link(config);
    ReplaySeekApplier applier(&link);

    // Seed a tlog-snapshot mission so the "restore to empty" branch has something to undo.
    const QList<mavlink_mission_item_int_t> items{_makeWaypoint(0)};
    QVERIFY(QMetaObject::invokeMethod(&link, "replayMissionUploaded",
                                       Q_ARG(int, MAV_MISSION_TYPE_MISSION),
                                       Q_ARG(QList<mavlink_mission_item_int_t>, items)));
    QCOMPARE(vehicle()->missionManager()->missionItems().count(), 1);

    // No plan file registered for this vehicle (Vehicle::peekReplayPlanFile is empty by
    // default), so a seek with no resolved mission-type entries restores to empty.
    const MissionItemsByType resolvedByType;
    QVERIFY(QMetaObject::invokeMethod(&link, "replaySeekMissionResolved",
                                       Q_ARG(MissionItemsByType, resolvedByType)));

    QCOMPARE(vehicle()->missionManager()->missionItems().count(), 0);
}

void ReplaySeekApplierTest::_seekParamResolvedSetsValue_test()
{
    SharedLinkConfigurationPtr config = std::make_shared<LogReplayConfiguration>(QStringLiteral("ReplaySeekApplierTest"));
    // Declaration order matters: applier is a QObject child of link, so link must
    // outlive it here — reversing this order would double-delete applier on unwind.
    LogReplayLink link(config);
    ReplaySeekApplier applier(&link);

    QVERIFY(waitForParametersReady());
    Fact* const fact = vehicle()->parameterManager()->getParameter(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("BAT1_V_CHARGED"));
    QVERIFY(fact);

    mavlink_param_union_t pu;
    pu.param_float = 12.5f;

    ParamSeekValue seekValue;
    seekValue.compId = MAV_COMP_ID_AUTOPILOT1;
    seekValue.paramId = QStringLiteral("BAT1_V_CHARGED");
    seekValue.rawValue = pu.param_float;
    seekValue.paramType = MAV_PARAM_TYPE_REAL32;
    seekValue.resetToInitial = false;

    QVERIFY(QMetaObject::invokeMethod(&link, "replaySeekParamResolved",
                                       Q_ARG(int, vehicle()->id()),
                                       Q_ARG(QList<ParamSeekValue>, {seekValue})));

    QCOMPARE(fact->rawValue().toFloat(), 12.5f);
}

void ReplaySeekApplierTest::_seekParamResolvedIgnoresOtherVehicle_test()
{
    SharedLinkConfigurationPtr config = std::make_shared<LogReplayConfiguration>(QStringLiteral("ReplaySeekApplierTest"));
    // Declaration order matters: applier is a QObject child of link, so link must
    // outlive it here — reversing this order would double-delete applier on unwind.
    LogReplayLink link(config);
    ReplaySeekApplier applier(&link);

    QVERIFY(waitForParametersReady());
    Fact* const fact = vehicle()->parameterManager()->getParameter(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("BAT1_V_CHARGED"));
    QVERIFY(fact);
    const QVariant before = fact->rawValue();

    mavlink_param_union_t pu;
    pu.param_float = 99.0f;

    ParamSeekValue seekValue;
    seekValue.compId = MAV_COMP_ID_AUTOPILOT1;
    seekValue.paramId = QStringLiteral("BAT1_V_CHARGED");
    seekValue.rawValue = pu.param_float;
    seekValue.paramType = MAV_PARAM_TYPE_REAL32;
    seekValue.resetToInitial = false;

    QVERIFY(QMetaObject::invokeMethod(&link, "replaySeekParamResolved",
                                       Q_ARG(int, vehicle()->id() + 1),
                                       Q_ARG(QList<ParamSeekValue>, {seekValue})));

    QCOMPARE(fact->rawValue(), before);
}

UT_REGISTER_TEST(ReplaySeekApplierTest, TestLabel::Integration, TestLabel::Vehicle)
