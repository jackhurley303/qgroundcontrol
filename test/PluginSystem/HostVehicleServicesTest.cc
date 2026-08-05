#include "HostVehicleServicesTest.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtTest/QSignalSpy>

#include "Fact.h"
#include "FactMetaData.h"
#include "HostServices/QGCMissionServiceImpl.h"
#include "HostServices/QGCParameterDiffServiceImpl.h"
#include "HostServices/QGCParameterServiceImpl.h"
#include "HostServices/QGCVehicleServiceImpl.h"
#include "ParameterManager.h"
#include "Vehicle.h"

void HostVehicleServicesTest::_vehicleServiceExposesVehicle_test()
{
    QGCVehicleServiceImpl service;

    QCOMPARE(service.activeVehicle(), vehicle());
    QVERIFY(service.vehicles().contains(vehicle()));

    // Plugins consume the handed-over vehicle through the meta-object surface
    QCOMPARE(service.activeVehicle()->property("id").toInt(), vehicle()->id());
}

void HostVehicleServicesTest::_vehicleServiceRelaysRemoval_test()
{
    QGCVehicleServiceImpl service;
    QObject* connected = vehicle();

    QSignalSpy removedSpy(&service, &QGCVehicleService::vehicleRemoved);
    QSignalSpy activeSpy(&service, &QGCVehicleService::activeVehicleChanged);

    _disconnectMockLink();

    QVERIFY(removedSpy.count() >= 1);
    // Pointer identity only — the vehicle is destroyed after removal
    QCOMPARE(removedSpy.last().at(0).value<QObject*>(), connected);
    QVERIFY(activeSpy.count() >= 1);
    QVERIFY(!service.activeVehicle());
    QVERIFY(service.vehicles().isEmpty());
}

void HostVehicleServicesTest::_missionServiceReadyAndSave_test()
{
    QGCMissionServiceImpl service;
    const int vehicleId = vehicle()->id();

    QVERIFY(waitForCondition([this] { return vehicle()->initialPlanRequestComplete(); },
                             TestTimeout::longMs(), QStringLiteral("initialPlanRequestComplete")));
    QVERIFY(service.missionReady(vehicleId));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString planPath = tempDir.filePath(QStringLiteral("snapshot.plan"));

    QVERIFY(service.saveVehicleMissionToFile(vehicleId, planPath));
    QVERIFY(QFileInfo::exists(planPath));
    QVERIFY(QFileInfo(planPath).size() > 0);

    // The snapshot must be a valid .plan JSON document
    QFile planFile(planPath);
    QVERIFY(planFile.open(QIODevice::ReadOnly));
    QVERIFY(QJsonDocument::fromJson(planFile.readAll()).isObject());
}

void HostVehicleServicesTest::_missionServiceRelaysReadyChanged_test()
{
    // The service seeds its per-vehicle watch from the already-connected vehicle
    QGCMissionServiceImpl service;
    QSignalSpy readySpy(&service, &QGCMissionService::missionReadyChanged);

    // Fire the vehicle's signal through the metaobject — the service must
    // relay it keyed by vehicle id
    QVERIFY(QMetaObject::invokeMethod(vehicle(), "initialPlanRequestCompleteChanged", Q_ARG(bool, true)));

    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.at(0).at(0).toInt(), vehicle()->id());
    QCOMPARE(readySpy.at(0).at(1).toBool(), true);
}

void HostVehicleServicesTest::_missionServiceUnknownVehicle_test()
{
    QGCMissionServiceImpl service;
    const int bogusId = vehicle()->id() + 1;

    QVERIFY(!service.missionReady(bogusId));
    QVERIFY(!service.saveVehicleMissionToFile(bogusId, QStringLiteral("/nonexistent/out.plan")));
}

void HostVehicleServicesTest::_parameterServiceReadyAndSave_test()
{
    QGCParameterServiceImpl service;
    const int vehicleId = vehicle()->id();

    QVERIFY(waitForParametersReady());
    QVERIFY(service.parametersReady(vehicleId));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString paramsPath = tempDir.filePath(QStringLiteral("snapshot.params"));

    QVERIFY(service.saveVehicleParametersToFile(vehicleId, paramsPath));
    QVERIFY(QFileInfo::exists(paramsPath));
    QVERIFY(QFileInfo(paramsPath).size() > 0);

    // The service only relocates writeParametersToStream behind a file-path
    // boundary — its output must be byte-identical to the direct call
    QString direct;
    QTextStream directStream(&direct);
    vehicle()->parameterManager()->writeParametersToStream(directStream);

    QFile paramsFile(paramsPath);
    QVERIFY(paramsFile.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(paramsFile.readAll()), direct);
}

void HostVehicleServicesTest::_parameterServiceRelaysReadyChanged_test()
{
    // The service seeds its per-vehicle watch from the already-connected vehicle
    QGCParameterServiceImpl service;
    QSignalSpy readySpy(&service, &QGCParameterService::parametersReadyChanged);

    // Fire the parameter manager's signal through the metaobject — the service
    // must relay it keyed by vehicle id
    QVERIFY(QMetaObject::invokeMethod(vehicle()->parameterManager(), "parametersReadyChanged", Q_ARG(bool, true)));

    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.at(0).at(0).toInt(), vehicle()->id());
    QCOMPARE(readySpy.at(0).at(1).toBool(), true);
}

void HostVehicleServicesTest::_parameterServiceUnknownVehicle_test()
{
    QGCParameterServiceImpl service;
    const int bogusId = vehicle()->id() + 1;

    QVERIFY(!service.parametersReady(bogusId));
    QVERIFY(!service.saveVehicleParametersToFile(bogusId, QStringLiteral("/nonexistent/out.params")));
}

Fact* HostVehicleServicesTest::_diffableParameter()
{
    ParameterManager* parameterMgr = vehicle()->parameterManager();
    const int componentId = vehicle()->defaultComponentId();

    // Integer-typed so perturbing the value round-trips exactly through the
    // file's decimal text, non-enumerated so the display string is that same
    // text, and writable so a diff entry is even produced.
    for (const QString& name : parameterMgr->parameterNames(componentId)) {
        Fact* fact = parameterMgr->getParameter(componentId, name);
        if (fact->readOnly() || !fact->enumStrings().isEmpty()) {
            continue;
        }
        switch (fact->type()) {
        case FactMetaData::valueTypeUint8:
        case FactMetaData::valueTypeInt8:
        case FactMetaData::valueTypeUint16:
        case FactMetaData::valueTypeInt16:
        case FactMetaData::valueTypeUint32:
        case FactMetaData::valueTypeInt32:
            break;
        default:
            continue;
        }

        // The perturbation must survive the metadata's own range check (so
        // convertOnly is false — true is exactly the flag that skips it), or
        // the diff would legitimately reject it and the test prove nothing.
        QVariant typedValue;
        QString errorString;
        const QString candidate = QString::number(fact->rawValue().toInt() + 1);
        if (fact->metaData()->convertAndValidateRaw(candidate, false /* convertOnly */, typedValue, errorString)) {
            return fact;
        }
    }

    return nullptr;
}

bool HostVehicleServicesTest::_writeParamFile(const QString& filePath, const QStringList& lines)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    for (const QString& line : lines) {
        stream << line << "\n";
    }
    return stream.status() == QTextStream::Ok;
}

QString HostVehicleServicesTest::_qgcParamLine(int vehicleId, int componentId, const QString& name, const QString& value, int mavType)
{
    return QStringLiteral("%1\t%2\t%3\t%4\t%5").arg(vehicleId).arg(componentId).arg(name, value).arg(mavType);
}

void HostVehicleServicesTest::_parameterDiffRoundTrip_test()
{
    QGCParameterDiffServiceImpl service;
    const int vehicleId = vehicle()->id();
    const int componentId = vehicle()->defaultComponentId();

    QVERIFY(waitForParametersReady());
    Fact* fact = _diffableParameter();
    QVERIFY2(fact, "No writable non-enumerated integer parameter on the mock vehicle");

    const QString paramName = fact->name();
    const QString vehicleValueString = fact->enumOrValueString();
    const int newValue = fact->rawValue().toInt() + 1;

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString paramsPath = tempDir.filePath(QStringLiteral("diff.params"));
    QVERIFY(_writeParamFile(paramsPath, {
        QStringLiteral("# Onboard parameters"),
        _qgcParamLine(vehicleId, componentId, paramName, QString::number(newValue),
                      ParameterManager::factTypeToMavType(fact->type())),
    }));

    const QVariantMap result = service.diffParametersFromFile(vehicleId, paramsPath);
    QCOMPARE(result.value(QGCParameterDiffResult::error).toString(), QString());
    QVERIFY(result.value(QGCParameterDiffResult::missingParameters).toStringList().isEmpty());
    QVERIFY(result.value(QGCParameterDiffResult::invalidParameters).toStringList().isEmpty());
    QCOMPARE(result.value(QGCParameterDiffResult::otherVehicle).toBool(), false);
    QCOMPARE(result.value(QGCParameterDiffResult::multipleComponents).toBool(), false);

    const QVariantList entries = result.value(QGCParameterDiffResult::entries).toList();
    QCOMPARE(entries.count(), 1);

    const QVariantMap entry = entries.first().toMap();
    QCOMPARE(entry.value(QGCParameterDiffEntry::name).toString(), paramName);
    QCOMPARE(entry.value(QGCParameterDiffEntry::componentId).toInt(), componentId);
    QCOMPARE(entry.value(QGCParameterDiffEntry::onVehicle).toBool(), true);
    QCOMPARE(entry.value(QGCParameterDiffEntry::fileValue).toString(), QString::number(newValue));
    QCOMPARE(entry.value(QGCParameterDiffEntry::vehicleValue).toString(), vehicleValueString);
    QCOMPARE(entry.value(QGCParameterDiffEntry::units).toString(), fact->cookedUnits());

    // The entry goes back unmodified — that round trip is the contract. The
    // write must be waited out, not just issued: PARAM_SET is a state machine,
    // and a test returning before the vehicle acks tears the link down under it.
    QSignalSpy vehicleUpdatedSpy(fact, &Fact::vehicleUpdated);
    QCOMPARE(service.writeParameterDiff(vehicleId, entries), 1);
    QVERIFY(waitForSignal(vehicleUpdatedSpy, TestTimeout::mediumMs(), QStringLiteral("Fact::vehicleUpdated")));
    QCOMPARE(fact->rawValue().toInt(), newValue);

    // Re-applying the same entry sends nothing, so nothing may be counted
    QCOMPARE(service.writeParameterDiff(vehicleId, entries), 0);
}

void HostVehicleServicesTest::_parameterDiffUnchangedFileHasNoEntries_test()
{
    QGCParameterDiffServiceImpl service;
    const int vehicleId = vehicle()->id();

    QVERIFY(waitForParametersReady());

    // The vehicle's own snapshot: a real comparison must find nothing to change
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString paramsPath = tempDir.filePath(QStringLiteral("snapshot.params"));
    QGCParameterServiceImpl snapshotService;
    QVERIFY(snapshotService.saveVehicleParametersToFile(vehicleId, paramsPath));

    const QVariantMap result = service.diffParametersFromFile(vehicleId, paramsPath);
    QCOMPARE(result.value(QGCParameterDiffResult::error).toString(), QString());
    QVERIFY(result.value(QGCParameterDiffResult::entries).toList().isEmpty());
    QVERIFY(result.value(QGCParameterDiffResult::invalidParameters).toStringList().isEmpty());
    QCOMPARE(result.value(QGCParameterDiffResult::otherVehicle).toBool(), false);
}

void HostVehicleServicesTest::_parameterDiffOffVehicleParameter_test()
{
    QGCParameterDiffServiceImpl service;
    const int vehicleId = vehicle()->id();
    const int componentId = vehicle()->defaultComponentId();
    const QString unknownName = QStringLiteral("QGC_DIFF_TEST_UNKNOWN");

    QVERIFY(waitForParametersReady());
    QVERIFY(!vehicle()->parameterManager()->parameterExists(componentId, unknownName));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString paramsPath = tempDir.filePath(QStringLiteral("unknown.params"));
    QVERIFY(_writeParamFile(paramsPath, {
        _qgcParamLine(vehicleId, componentId, unknownName, QStringLiteral("7"), MAV_PARAM_TYPE_INT32),
    }));

    // QGC format carries the type, so an unknown parameter is still writable —
    // it is reported as an entry, not as missing
    const QVariantMap result = service.diffParametersFromFile(vehicleId, paramsPath);
    QCOMPARE(result.value(QGCParameterDiffResult::error).toString(), QString());
    QVERIFY(result.value(QGCParameterDiffResult::missingParameters).toStringList().isEmpty());

    const QVariantList entries = result.value(QGCParameterDiffResult::entries).toList();
    QCOMPARE(entries.count(), 1);

    const QVariantMap entry = entries.first().toMap();
    QCOMPARE(entry.value(QGCParameterDiffEntry::name).toString(), unknownName);
    QCOMPARE(entry.value(QGCParameterDiffEntry::onVehicle).toBool(), false);
    QCOMPARE(entry.value(QGCParameterDiffEntry::vehicleValue).toString(), QString());
    QCOMPARE(entry.value(QGCParameterDiffEntry::units).toString(), QString());
    QCOMPARE(entry.value(QGCParameterDiffEntry::mavType).toInt(), static_cast<int>(MAV_PARAM_TYPE_INT32));

    // Typed from the file's MAVLink type, not left as the file's text. A QString
    // here would reach ParameterManager's ack matcher, which compares QVariant
    // types and would throw away the vehicle's own acknowledgement — the write
    // would land and still be reported to the user as failed.
    QCOMPARE(entry.value(QGCParameterDiffEntry::rawValue).typeId(), QMetaType::Int);
    QCOMPARE(entry.value(QGCParameterDiffEntry::rawValue).toInt(), 7);

    // The raw PARAM_SET this entry would take is deliberately not driven here:
    // MockLink Q_ASSERTs on a PARAM_SET naming a parameter it doesn't hold, so
    // the send itself cannot be exercised against this mock. What is checkable
    // is the branch *selection* and its guards — an off-vehicle entry with no
    // usable MAVLink type, or none that its value converts to, has nothing to
    // encode and must be skipped rather than put a zero-filled frame on the wire.
    QVariantMap untypedEntry = entry;
    untypedEntry.remove(QGCParameterDiffEntry::mavType);
    QVariantMap unconvertibleEntry = entry;
    unconvertibleEntry[QGCParameterDiffEntry::rawValue] = QStringLiteral("5.5");
    unconvertibleEntry[QGCParameterDiffEntry::mavType] = static_cast<int>(MAV_PARAM_TYPE_INT32);
    QVariantMap bogusTypeEntry = entry;
    bogusTypeEntry[QGCParameterDiffEntry::mavType] = 99;
    // A real MAV_PARAM_TYPE, but one a classic PARAM_SET cannot carry — its
    // param_value is a single float32. Letting it through does not fail
    // cleanly: ParameterManager's encoder packs an int32 under a 64-bit type
    // header, or sends an unpacked frame when even that conversion fails.
    QVariantMap wideTypeEntry = entry;
    wideTypeEntry[QGCParameterDiffEntry::mavType] = static_cast<int>(MAV_PARAM_TYPE_INT64);

    for (const QVariantMap& rejected : {untypedEntry, unconvertibleEntry, bogusTypeEntry, wideTypeEntry}) {
        expectLogMessage("PluginSystem.QGCHostServices", QtWarningMsg,
                         QRegularExpression("Ignoring off-vehicle parameter diff entry with no usable MAVLink type/value"));
        QCOMPARE(service.writeParameterDiff(vehicleId, QVariantList{rejected}), 0);
        verifyExpectedLogMessage();
    }
}

void HostVehicleServicesTest::_parameterDiffRefusesReadOnlyWrite_test()
{
    QGCParameterDiffServiceImpl service;
    const int vehicleId = vehicle()->id();
    const int componentId = vehicle()->defaultComponentId();

    QVERIFY(waitForParametersReady());
    Fact* fact = _diffableParameter();
    QVERIFY2(fact, "No writable non-enumerated integer parameter on the mock vehicle");

    const QVariant originalValue = fact->rawValue();
    QVariantMap entry;
    entry[QGCParameterDiffEntry::componentId] = componentId;
    entry[QGCParameterDiffEntry::name] = fact->name();
    entry[QGCParameterDiffEntry::rawValue] = originalValue.toInt() + 1;

    // The diff refuses read-only parameters, but the value-typed boundary means
    // an entry can also be hand-built or held while the vehicle's own metadata
    // moves on — so the write has to refuse them too, not inherit the promise.
    fact->metaData()->setReadOnly(true);
    expectLogMessage("PluginSystem.QGCHostServices", QtWarningMsg,
                     QRegularExpression("Ignoring parameter diff entry for a read-only parameter"));
    const int written = service.writeParameterDiff(vehicleId, QVariantList{entry});
    verifyExpectedLogMessage();
    fact->metaData()->setReadOnly(false);

    QCOMPARE(written, 0);
    QCOMPARE(fact->rawValue(), originalValue);
}

void HostVehicleServicesTest::_parameterDiffMissionPlannerFormat_test()
{
    QGCParameterDiffServiceImpl service;
    const int vehicleId = vehicle()->id();

    QVERIFY(waitForParametersReady());
    Fact* fact = _diffableParameter();
    QVERIFY2(fact, "No writable non-enumerated integer parameter on the mock vehicle");

    const int newValue = fact->rawValue().toInt() + 1;
    const QString unknownName = QStringLiteral("QGC_DIFF_TEST_UNKNOWN");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString paramsPath = tempDir.filePath(QStringLiteral("mp.params"));
    QVERIFY(_writeParamFile(paramsPath, {
        QStringLiteral("%1,%2").arg(fact->name(), QString::number(newValue)),
        QStringLiteral("%1,%2").arg(unknownName, QStringLiteral("7")),
    }));

    // Two columns carry no type, so a parameter the vehicle doesn't have
    // cannot be written — it is reported as missing instead of as an entry
    const QVariantMap result = service.diffParametersFromFile(vehicleId, paramsPath);
    QCOMPARE(result.value(QGCParameterDiffResult::error).toString(), QString());
    QCOMPARE(result.value(QGCParameterDiffResult::missingParameters).toStringList(), QStringList{unknownName});

    const QVariantList entries = result.value(QGCParameterDiffResult::entries).toList();
    QCOMPARE(entries.count(), 1);
    QCOMPARE(entries.first().toMap().value(QGCParameterDiffEntry::name).toString(), fact->name());
    QCOMPARE(entries.first().toMap().value(QGCParameterDiffEntry::onVehicle).toBool(), true);
}

void HostVehicleServicesTest::_parameterDiffRejectsUnusableValues_test()
{
    QGCParameterDiffServiceImpl service;
    const int vehicleId = vehicle()->id();
    const int componentId = vehicle()->defaultComponentId();

    QVERIFY(waitForParametersReady());
    Fact* fact = _diffableParameter();
    QVERIFY2(fact, "No writable non-enumerated integer parameter on the mock vehicle");

    const QVariant originalValue = fact->rawValue();

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString paramsPath = tempDir.filePath(QStringLiteral("garbage.params"));
    const QString offVehicleName = QStringLiteral("QGC_DIFF_TEST_UNKNOWN");
    const QString wideTypeName = QStringLiteral("QGC_DIFF_TEST_WIDE");
    QVERIFY(_writeParamFile(paramsPath, {
        _qgcParamLine(vehicleId, componentId, fact->name(), QStringLiteral("not-a-number"),
                      ParameterManager::factTypeToMavType(fact->type())),
        // Off-vehicle: nothing to borrow metadata from, so the file's own
        // MAVLink type is what "5.5 is not an int32" has to be judged against
        _qgcParamLine(vehicleId, componentId, offVehicleName, QStringLiteral("5.5"), MAV_PARAM_TYPE_INT32),
        // A type a classic PARAM_SET cannot carry at all, rejected at parse
        // rather than left for the encoder to mangle
        _qgcParamLine(vehicleId, componentId, wideTypeName, QStringLiteral("5"), MAV_PARAM_TYPE_INT64),
    }));

    // A value that cannot become the parameter's type is reported, never turned
    // into a diff row promising a change the write could not make
    const QVariantMap result = service.diffParametersFromFile(vehicleId, paramsPath);
    QCOMPARE(result.value(QGCParameterDiffResult::error).toString(), QString());
    QVERIFY(result.value(QGCParameterDiffResult::entries).toList().isEmpty());
    QCOMPARE(result.value(QGCParameterDiffResult::invalidParameters).toStringList(),
             QStringList({fact->name(), offVehicleName, wideTypeName}));

    // And a hand-built entry carrying the same value is skipped by the write
    QVariantMap entry;
    entry[QGCParameterDiffEntry::componentId] = componentId;
    entry[QGCParameterDiffEntry::name] = fact->name();
    entry[QGCParameterDiffEntry::rawValue] = QStringLiteral("not-a-number");
    expectLogMessage("PluginSystem.QGCHostServices", QtWarningMsg,
                     QRegularExpression("Ignoring parameter diff entry with an unusable value"));
    QCOMPARE(service.writeParameterDiff(vehicleId, QVariantList{entry}), 0);
    verifyExpectedLogMessage();
    QCOMPARE(fact->rawValue(), originalValue);
}

void HostVehicleServicesTest::_parameterDiffReportsFileFlags_test()
{
    QGCParameterDiffServiceImpl service;
    const int vehicleId = vehicle()->id();
    const int componentId = vehicle()->defaultComponentId();

    QVERIFY(waitForParametersReady());

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString paramsPath = tempDir.filePath(QStringLiteral("flags.params"));
    QVERIFY(_writeParamFile(paramsPath, {
        _qgcParamLine(vehicleId + 1, componentId, QStringLiteral("QGC_DIFF_TEST_A"), QStringLiteral("1"), MAV_PARAM_TYPE_INT32),
        _qgcParamLine(vehicleId + 1, componentId + 1, QStringLiteral("QGC_DIFF_TEST_B"), QStringLiteral("2"), MAV_PARAM_TYPE_INT32),
    }));

    const QVariantMap result = service.diffParametersFromFile(vehicleId, paramsPath);
    QCOMPARE(result.value(QGCParameterDiffResult::error).toString(), QString());
    QCOMPARE(result.value(QGCParameterDiffResult::otherVehicle).toBool(), true);
    QCOMPARE(result.value(QGCParameterDiffResult::multipleComponents).toBool(), true);
    QCOMPARE(result.value(QGCParameterDiffResult::entries).toList().count(), 2);
}

void HostVehicleServicesTest::_parameterDiffRejectsBadInput_test()
{
    QGCParameterDiffServiceImpl service;
    const int vehicleId = vehicle()->id();
    const int bogusId = vehicleId + 1;

    QVERIFY(waitForParametersReady());

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Unknown vehicle
    const QString missingPath = tempDir.filePath(QStringLiteral("absent.params"));
    QVariantMap result = service.diffParametersFromFile(bogusId, missingPath);
    QVERIFY(!result.value(QGCParameterDiffResult::error).toString().isEmpty());
    QVERIFY(result.value(QGCParameterDiffResult::entries).toList().isEmpty());
    QCOMPARE(service.writeParameterDiff(bogusId, QVariantList()), -1);

    // Unreadable file
    result = service.diffParametersFromFile(vehicleId, missingPath);
    QVERIFY(!result.value(QGCParameterDiffResult::error).toString().isEmpty());
    QVERIFY(result.value(QGCParameterDiffResult::entries).toList().isEmpty());

    // Readable, but nothing a parameter file parser can use
    const QString garbagePath = tempDir.filePath(QStringLiteral("prose.params"));
    QVERIFY(_writeParamFile(garbagePath, {
        QStringLiteral("# only a comment"),
        QStringLiteral("three columns here"),
    }));
    result = service.diffParametersFromFile(vehicleId, garbagePath);
    QVERIFY(!result.value(QGCParameterDiffResult::error).toString().isEmpty());
    QVERIFY(result.value(QGCParameterDiffResult::entries).toList().isEmpty());

    // An entry with no name is skipped rather than written
    expectLogMessage("PluginSystem.QGCHostServices", QtWarningMsg,
                     QRegularExpression("Ignoring parameter diff entry with no name"));
    QCOMPARE(service.writeParameterDiff(vehicleId, QVariantList{QVariantMap()}), 0);
    verifyExpectedLogMessage();
}

UT_REGISTER_TEST(HostVehicleServicesTest, TestLabel::Integration, TestLabel::Vehicle)
