/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCParameterDiffServiceImpl.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>

#include "Fact.h"
#include "FactMetaData.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "QGCHostServicesImpl.h"
#include "Vehicle.h"

namespace {

/// Every key is present in every result, so a caller never has to distinguish
/// "absent" from "empty".
QVariantMap emptyResult()
{
    QVariantMap result;
    result[QGCParameterDiffResult::entries] = QVariantList();
    result[QGCParameterDiffResult::missingParameters] = QStringList();
    result[QGCParameterDiffResult::invalidParameters] = QStringList();
    result[QGCParameterDiffResult::otherVehicle] = false;
    result[QGCParameterDiffResult::multipleComponents] = false;
    result[QGCParameterDiffResult::error] = QString();
    return result;
}

QVariantMap errorResult(const QString& error)
{
    QVariantMap result = emptyResult();
    result[QGCParameterDiffResult::error] = error;
    return result;
}

/// Converts value to the type a MAVLink MAV_PARAM_TYPE names, using the same
/// FactMetaData conversion an on-vehicle parameter gets. Used where there is no
/// vehicle Fact to borrow metadata from.
/// @return false when mavType is not a type a PARAM_SET can carry, or value is
/// not convertible to it.
bool typedFromMavType(int mavType, const QVariant& value, QVariant& typedValue)
{
    // Deliberately narrower than the MAV_PARAM_TYPE enum: these are the seven
    // ParameterManager::_fillMavlinkParamUnion can encode. The 64-bit types are
    // unrepresentable in a classic PARAM_SET by construction — its param_value
    // is one float32 — and letting one through does not fail cleanly: the
    // encoder falls into its default arm, logs an internal error, and packs an
    // int32 under a 64-bit type header (or, when even that conversion fails,
    // leaves the message unpacked and sends it zero-filled). mavTypeToFactType()
    // is no guard either: it maps anything unrecognised to int32 with only a
    // log warning, so the check has to come first.
    switch (mavType) {
        case MAV_PARAM_TYPE_UINT8:
        case MAV_PARAM_TYPE_INT8:
        case MAV_PARAM_TYPE_UINT16:
        case MAV_PARAM_TYPE_INT16:
        case MAV_PARAM_TYPE_UINT32:
        case MAV_PARAM_TYPE_INT32:
        case MAV_PARAM_TYPE_REAL32:
            break;
        default:
            return false;
    }

    const FactMetaData metaData(ParameterManager::mavTypeToFactType(static_cast<MAV_PARAM_TYPE>(mavType)));
    QString conversionError;
    return metaData.convertAndValidateRaw(value, true /* convertOnly */, typedValue, conversionError);
}

/// The vehicle whose parameters are complete enough to diff against, or
/// nullptr. An incomplete cache would make every file line look like a
/// difference.
Vehicle* readyVehicle(int vehicleId)
{
    Vehicle* vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    if (!vehicle || !vehicle->parameterManager()->parametersReady()) {
        return nullptr;
    }
    return vehicle;
}

}  // namespace

QGCParameterDiffServiceImpl::QGCParameterDiffServiceImpl(QObject* parent) : QGCParameterDiffService(parent) {}

QVariantMap QGCParameterDiffServiceImpl::diffParametersFromFile(int vehicleId, const QString& filePath)
{
    Vehicle* vehicle = readyVehicle(vehicleId);
    if (!vehicle) {
        return errorResult(tr("No vehicle %1 with parameters ready.").arg(vehicleId));
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return errorResult(tr("Unable to open file: %1").arg(filePath));
    }

    ParameterManager* parameterMgr = vehicle->parameterManager();
    QVariantMap result = emptyResult();
    QVariantList entries;
    QStringList missingParams;
    QStringList invalidParams;
    bool otherVehicle = false;
    bool multipleComponents = false;

    static const QRegularExpression fieldSeparator("[\\t ,]+");

    QTextStream stream(&file);
    int parsedLineCount = 0;
    int firstComponentId = -1;

    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.startsWith("#") || line.trimmed().isEmpty()) {
            continue;
        }

        const QStringList fields = line.trimmed().split(fieldSeparator);

        int componentId = -1;
        QString paramName;
        QString fileValueStr;
        int mavParamType = -1;
        bool isMPFormat = false;

        if (fields.size() == 5) {
            // QGC tab-delimited: VehicleId ComponentId Name Value Type
            const int fileVehicleId = fields.at(0).toInt();
            componentId = fields.at(1).toInt();
            paramName = fields.at(2);
            fileValueStr = fields.at(3);
            mavParamType = fields.at(4).toInt();

            if (vehicle->id() != fileVehicleId) {
                otherVehicle = true;
            }
            if (firstComponentId == -1) {
                firstComponentId = componentId;
            } else if (firstComponentId != componentId) {
                multipleComponents = true;
            }
        } else if (fields.size() == 2) {
            // Mission Planner 2-column: Name Value
            paramName = fields.at(0);
            fileValueStr = fields.at(1);
            componentId = ParameterManager::defaultComponentId;
            isMPFormat = true;
        } else {
            continue;
        }

        parsedLineCount++;

        QString vehicleValueStr;
        QString units;
        QVariant rawValue;
        bool onVehicle = true;

        if (parameterMgr->parameterExists(componentId, paramName)) {
            Fact* vehicleFact = parameterMgr->getParameter(componentId, paramName);
            FactMetaData* metaData = vehicleFact->metaData();

            if (vehicleFact->readOnly()) {
                continue;
            }

            if (mavParamType == -1) {
                mavParamType = ParameterManager::factTypeToMavType(vehicleFact->type());
            }

            // Validated up front rather than left to Fact::setRawValue, which
            // silently keeps its old value when the conversion fails — that
            // would put a row in the diff claiming a change the write can
            // never make.
            QVariant typedValue;
            QString conversionError;
            if (!metaData->convertAndValidateRaw(fileValueStr, true /* convertOnly */, typedValue, conversionError)) {
                invalidParams.append(paramName);
                continue;
            }

            // Display strings come from a Fact carrying the vehicle's own
            // metadata, so enum names, decimal places and units match what the
            // host shows. Reboot messaging is suppressed across the write: the
            // metadata is shared with the live vehicle Fact, and this one is a
            // scratch value never sent anywhere.
            Fact fileFact(vehicleFact->componentId(), vehicleFact->name(), vehicleFact->type());
            const bool vehicleRebootRequired = metaData->vehicleRebootRequired();
            metaData->setVehicleRebootRequired(false);
            fileFact.setMetaData(metaData);
            fileFact.setRawValue(typedValue);
            metaData->setVehicleRebootRequired(vehicleRebootRequired);

            if (vehicleFact->rawValue() == fileFact.rawValue()) {
                continue;
            }

            fileValueStr = fileFact.enumOrValueString();
            rawValue = fileFact.rawValue();
            vehicleValueStr = vehicleFact->enumOrValueString();
            units = vehicleFact->cookedUnits();
        } else if (isMPFormat) {
            // Mission Planner format carries no type, so a parameter the
            // vehicle doesn't have cannot be written — report it instead.
            missingParams.append(paramName);
            continue;
        } else {
            // No vehicle Fact to type the value against, so type it from the
            // file's own MAVLink type. Leaving it as the file's text would put
            // a QString on the wire path, where ParameterManager's ack matcher
            // compares QVariant types and would discard the vehicle's perfectly
            // good acknowledgement as "not mine".
            if (!typedFromMavType(mavParamType, fileValueStr, rawValue)) {
                invalidParams.append(paramName);
                continue;
            }
            onVehicle = false;
        }

        QVariantMap entry;
        entry[QGCParameterDiffEntry::componentId] = componentId;
        entry[QGCParameterDiffEntry::name] = paramName;
        entry[QGCParameterDiffEntry::fileValue] = fileValueStr;
        entry[QGCParameterDiffEntry::vehicleValue] = vehicleValueStr;
        entry[QGCParameterDiffEntry::onVehicle] = onVehicle;
        entry[QGCParameterDiffEntry::units] = units;
        entry[QGCParameterDiffEntry::mavType] = mavParamType;
        entry[QGCParameterDiffEntry::rawValue] = rawValue;
        entries.append(entry);
    }

    if (parsedLineCount == 0) {
        return errorResult(
            tr("No valid parameters found in file. Check that the file is in QGC or Mission Planner format."));
    }

    result[QGCParameterDiffResult::entries] = entries;
    result[QGCParameterDiffResult::missingParameters] = missingParams;
    result[QGCParameterDiffResult::invalidParameters] = invalidParams;
    result[QGCParameterDiffResult::otherVehicle] = otherVehicle;
    result[QGCParameterDiffResult::multipleComponents] = multipleComponents;
    return result;
}

int QGCParameterDiffServiceImpl::writeParameterDiff(int vehicleId, const QVariantList& entries)
{
    Vehicle* vehicle = readyVehicle(vehicleId);
    if (!vehicle) {
        return -1;
    }

    ParameterManager* parameterMgr = vehicle->parameterManager();
    int written = 0;

    for (const QVariant& item : entries) {
        const QVariantMap entry = item.toMap();

        const QString paramName = entry.value(QGCParameterDiffEntry::name).toString();
        if (paramName.isEmpty()) {
            qCWarning(QGCHostServicesLog) << "Ignoring parameter diff entry with no name";
            continue;
        }

        bool ok = false;
        const int componentId = entry.value(QGCParameterDiffEntry::componentId).toInt(&ok);
        if (!ok) {
            qCWarning(QGCHostServicesLog) << "Ignoring parameter diff entry with no component id:" << paramName;
            continue;
        }

        const QVariant rawValue = entry.value(QGCParameterDiffEntry::rawValue);

        // Every entry is re-validated against the vehicle rather than trusted:
        // the value-typed boundary means a caller can hand back an entry it
        // built itself, or one held across the vehicle's own state moving on.
        // The diff's guarantees are not the write's.
        if (parameterMgr->parameterExists(componentId, paramName)) {
            Fact* fact = parameterMgr->getParameter(componentId, paramName);
            if (fact->readOnly()) {
                qCWarning(QGCHostServicesLog)
                    << "Ignoring parameter diff entry for a read-only parameter:" << paramName;
                continue;
            }

            QVariant typedValue;
            QString conversionError;
            if (!fact->metaData()->convertAndValidateRaw(rawValue, true /* convertOnly */, typedValue,
                                                         conversionError)) {
                qCWarning(QGCHostServicesLog)
                    << "Ignoring parameter diff entry with an unusable value:" << paramName << conversionError;
                continue;
            }

            // setRawValue is a no-op when the value already matches, so nothing
            // reaches the vehicle and nothing may be counted as sent
            if (typedValue == fact->rawValue()) {
                continue;
            }
            fact->setRawValue(typedValue);
        } else {
            QVariant typedValue;
            const int mavType = entry.value(QGCParameterDiffEntry::mavType).toInt(&ok);
            if (!ok || !typedFromMavType(mavType, rawValue, typedValue)) {
                qCWarning(QGCHostServicesLog)
                    << "Ignoring off-vehicle parameter diff entry with no usable MAVLink type/value:" << paramName;
                continue;
            }
            parameterMgr->_mavlinkParamSet(componentId, paramName,
                                           ParameterManager::mavTypeToFactType(static_cast<MAV_PARAM_TYPE>(mavType)),
                                           typedValue);
        }

        written++;
    }

    return written;
}
