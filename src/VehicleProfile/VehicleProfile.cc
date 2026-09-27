#include "VehicleProfile.h"

#include <QtCore/QBuffer>
#include <QtCore/QIODevice>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QObject>
#include <QtCore/QUuid>
#include <QtGui/QImage>
#include <QtGui/QImageReader>

#include "JsonParsing.h"
#include "JsonValidation.h"
#include "QGCMAVLink.h"

namespace {

constexpr const char* kIdKey = "id";
constexpr const char* kNameKey = "name";
constexpr const char* kManufacturerKey = "manufacturer";
constexpr const char* kModelKey = "model";
constexpr const char* kStatusKey = "status";
constexpr const char* kStatusActiveValue = "active";
constexpr const char* kStatusInactiveValue = "inactive";
constexpr const char* kMavTypeKey = "mavType";
constexpr const char* kWeightKgKey = "weightKg";
constexpr const char* kMaxPayloadKgKey = "maxPayloadKg";
constexpr const char* kMaxFlightTimeMinutesKey = "maxFlightTimeMinutes";
constexpr const char* kRegistrationNumberKey = "registrationNumber";
constexpr const char* kSerialNumberKey = "serialNumber";
constexpr const char* kBatteriesKey = "batteries";
constexpr const char* kBatteryCellCountKey = "cellCount";
constexpr const char* kBatteryCapacityMahKey = "capacityMah";
constexpr const char* kSensorsKey = "sensors";
constexpr const char* kSensorTypeKey = "type";
constexpr const char* kSensorModelKey = "model";
constexpr const char* kFlightControllerKey = "flightController";
constexpr const char* kFCHardwareKey = "hardware";
constexpr const char* kFCFirmwareKey = "firmware";
constexpr const char* kFCFirmwareVersionKey = "firmwareVersion";
constexpr const char* kNotesKey = "notes";
constexpr const char* kImageKey = "image";
constexpr const char* kImageMimeTypeKey = "mimeType";
constexpr const char* kImageDataKey = "data";

/// One row of the `mavType` enum-name table: a vehicle MAV_TYPE paired with the exact token
/// the file stores for it.
struct MavTypeInfo
{
    QGCMAVLinkTypes::VehicleClass_t mavType;
    const char* enumName;
};

/// The `mavType` enum-name table. Nothing else in QGC maps a MAV_TYPE to its enum name:
/// QGCMAVLink::vehicleClassToInternalString() only names the eight coarse VehicleClass_t
/// categories (e.g. "MultiRotor" for a value that could be MAV_TYPE_QUADROTOR or
/// MAV_TYPE_HEXAROTOR), never the 23 specific types this file format stores. This table is
/// that missing mapping, kept local to the file format rather than added to the upstream
/// QGCMAVLink class. Excludes the non-vehicle types (MAV_TYPE_GCS, MAV_TYPE_ANTENNA_TRACKER,
/// MAV_TYPE_ONBOARD_CONTROLLER, MAV_TYPE_GIMBAL, MAV_TYPE_ADSB, MAV_TYPE_VTOL_RESERVED5), which
/// a `.vehicle` file must never claim to be.
const QList<MavTypeInfo>& mavTypeTable()
{
    static const QList<MavTypeInfo> table = {
        {MAV_TYPE_GENERIC, "MAV_TYPE_GENERIC"},
        {MAV_TYPE_FIXED_WING, "MAV_TYPE_FIXED_WING"},
        {MAV_TYPE_QUADROTOR, "MAV_TYPE_QUADROTOR"},
        {MAV_TYPE_COAXIAL, "MAV_TYPE_COAXIAL"},
        {MAV_TYPE_HELICOPTER, "MAV_TYPE_HELICOPTER"},
        {MAV_TYPE_AIRSHIP, "MAV_TYPE_AIRSHIP"},
        {MAV_TYPE_FREE_BALLOON, "MAV_TYPE_FREE_BALLOON"},
        {MAV_TYPE_ROCKET, "MAV_TYPE_ROCKET"},
        {MAV_TYPE_GROUND_ROVER, "MAV_TYPE_GROUND_ROVER"},
        {MAV_TYPE_SURFACE_BOAT, "MAV_TYPE_SURFACE_BOAT"},
        {MAV_TYPE_SUBMARINE, "MAV_TYPE_SUBMARINE"},
        {MAV_TYPE_SPACECRAFT_ORBITER, "MAV_TYPE_SPACECRAFT_ORBITER"},
        {MAV_TYPE_HEXAROTOR, "MAV_TYPE_HEXAROTOR"},
        {MAV_TYPE_OCTOROTOR, "MAV_TYPE_OCTOROTOR"},
        {MAV_TYPE_TRICOPTER, "MAV_TYPE_TRICOPTER"},
        {MAV_TYPE_FLAPPING_WING, "MAV_TYPE_FLAPPING_WING"},
        {MAV_TYPE_KITE, "MAV_TYPE_KITE"},
        {MAV_TYPE_VTOL_TAILSITTER_DUOROTOR, "MAV_TYPE_VTOL_TAILSITTER_DUOROTOR"},
        {MAV_TYPE_VTOL_TAILSITTER_QUADROTOR, "MAV_TYPE_VTOL_TAILSITTER_QUADROTOR"},
        {MAV_TYPE_VTOL_TILTROTOR, "MAV_TYPE_VTOL_TILTROTOR"},
        {MAV_TYPE_VTOL_FIXEDROTOR, "MAV_TYPE_VTOL_FIXEDROTOR"},
        {MAV_TYPE_VTOL_TAILSITTER, "MAV_TYPE_VTOL_TAILSITTER"},
        {MAV_TYPE_VTOL_TILTWING, "MAV_TYPE_VTOL_TILTWING"},
    };
    return table;
}

QString mavTypeToEnumName(QGCMAVLinkTypes::VehicleClass_t mavType)
{
    for (const MavTypeInfo& info : mavTypeTable()) {
        if (info.mavType == mavType) {
            return QString::fromLatin1(info.enumName);
        }
    }
    return QString();
}

bool mavTypeFromEnumName(const QString& value, QGCMAVLinkTypes::VehicleClass_t& mavType)
{
    for (const MavTypeInfo& info : mavTypeTable()) {
        if (QLatin1String(info.enumName) == value) {
            mavType = info.mavType;
            return true;
        }
    }
    return false;
}

/// One row of the `flightController.firmware` enum-name table.
struct FirmwareInfo
{
    QGCMAVLinkTypes::FirmwareClass_t firmware;
    const char* enumName;
};

/// The `firmware` enum-name table: the three FirmwareClass_t values this file format allows,
/// paired with the exact MAV_AUTOPILOT token each is stored as.
const QList<FirmwareInfo>& firmwareTable()
{
    static const QList<FirmwareInfo> table = {
        {MAV_AUTOPILOT_GENERIC, "MAV_AUTOPILOT_GENERIC"},
        {MAV_AUTOPILOT_ARDUPILOTMEGA, "MAV_AUTOPILOT_ARDUPILOTMEGA"},
        {MAV_AUTOPILOT_PX4, "MAV_AUTOPILOT_PX4"},
    };
    return table;
}

QString firmwareToEnumName(QGCMAVLinkTypes::FirmwareClass_t firmware)
{
    for (const FirmwareInfo& info : firmwareTable()) {
        if (info.firmware == firmware) {
            return QString::fromLatin1(info.enumName);
        }
    }
    return QString();
}

bool firmwareFromEnumName(const QString& value, QGCMAVLinkTypes::FirmwareClass_t& firmware)
{
    for (const FirmwareInfo& info : firmwareTable()) {
        if (QLatin1String(info.enumName) == value) {
            firmware = info.firmware;
            return true;
        }
    }
    return false;
}

/// Maps a QImageReader-detected format ("png", "jpeg", "bmp", "gif", ...) to its MIME type
/// for the `image.mimeType` field. Falls back to "image/<format>" for anything else the
/// platform's Qt image plugins can read, so an unusual but valid format is still recorded
/// rather than silently dropped.
QString mimeTypeForImageReaderFormat(const QByteArray& format)
{
    const QByteArray lower = format.toLower();
    if (lower == "png") {
        return QStringLiteral("image/png");
    }
    if (lower == "jpg" || lower == "jpeg") {
        return QStringLiteral("image/jpeg");
    }
    if (lower == "bmp") {
        return QStringLiteral("image/bmp");
    }
    if (lower == "gif") {
        return QStringLiteral("image/gif");
    }
    return QStringLiteral("image/%1").arg(QString::fromLatin1(lower));
}

}  // namespace

VehicleProfile::VehicleProfile()
    : _id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{}

const QList<QGCMAVLinkTypes::VehicleClass_t>& VehicleProfile::allowedMavTypes()
{
    static const QList<QGCMAVLinkTypes::VehicleClass_t> types = [] {
        QList<QGCMAVLinkTypes::VehicleClass_t> result;
        result.reserve(mavTypeTable().size());
        for (const MavTypeInfo& info : mavTypeTable()) {
            result.append(info.mavType);
        }
        return result;
    }();
    return types;
}

const QList<QGCMAVLinkTypes::FirmwareClass_t>& VehicleProfile::allowedFirmwareTypes()
{
    static const QList<QGCMAVLinkTypes::FirmwareClass_t> types = [] {
        QList<QGCMAVLinkTypes::FirmwareClass_t> result;
        result.reserve(firmwareTable().size());
        for (const FirmwareInfo& info : firmwareTable()) {
            result.append(info.firmware);
        }
        return result;
    }();
    return types;
}

bool VehicleProfile::loadJson(const QByteArray& bytes, QString& errorString)
{
    QJsonDocument jsonDoc;
    if (!JsonParsing::isJsonFile(bytes, jsonDoc, errorString)) {
        return false;
    }

    if (!jsonDoc.isObject()) {
        errorString = QObject::tr("Root of vehicle file is not an object");
        return false;
    }

    const QJsonObject json = jsonDoc.object();

    int version = 0;
    if (!JsonParsing::validateExternalQGCJsonFile(json, kFileType, kFileVersion, kFileVersion, version, errorString)) {
        return false;
    }

    static const QList<JsonParsing::KeyValidateInfo> rootKeys = {
        {kIdKey, QJsonValue::String, true},
        {kNameKey, QJsonValue::String, true},
        {kManufacturerKey, QJsonValue::String, false},
        {kModelKey, QJsonValue::String, false},
        {kStatusKey, QJsonValue::String, false},
        {kMavTypeKey, QJsonValue::String, true},
        {kWeightKgKey, QJsonValue::Double, false},
        {kMaxPayloadKgKey, QJsonValue::Double, false},
        {kMaxFlightTimeMinutesKey, QJsonValue::Double, false},
        {kRegistrationNumberKey, QJsonValue::String, false},
        {kSerialNumberKey, QJsonValue::String, false},
        {kBatteriesKey, QJsonValue::Array, false},
        {kFlightControllerKey, QJsonValue::Object, false},
        {kSensorsKey, QJsonValue::Array, false},
        {kNotesKey, QJsonValue::String, false},
        {kImageKey, QJsonValue::Object, false},
    };
    if (!JsonParsing::validateKeys(json, rootKeys, errorString)) {
        return false;
    }

    QGCMAVLinkTypes::VehicleClass_t parsedMavType = QGCMAVLinkTypes::VehicleClassGeneric;
    const QString mavTypeStr = json[kMavTypeKey].toString();
    if (!mavTypeFromEnumName(mavTypeStr, parsedMavType)) {
        errorString = QObject::tr("Unknown or non-vehicle mavType value: %1").arg(mavTypeStr);
        return false;
    }

    bool parsedActive = true;
    if (json.contains(kStatusKey)) {
        const QString statusStr = json[kStatusKey].toString();
        if (statusStr == QLatin1String(kStatusActiveValue)) {
            parsedActive = true;
        } else if (statusStr == QLatin1String(kStatusInactiveValue)) {
            parsedActive = false;
        } else {
            errorString = QObject::tr("Unknown status value: %1").arg(statusStr);
            return false;
        }
    }

    QList<Battery> batteries;
    if (json.contains(kBatteriesKey)) {
        const QJsonArray batteriesJson = json[kBatteriesKey].toArray();
        batteries.reserve(batteriesJson.size());
        for (qsizetype i = 0; i < batteriesJson.size(); ++i) {
            if (!batteriesJson[i].isObject()) {
                errorString = QObject::tr("batteries[%1] is not an object").arg(i);
                return false;
            }
            const QJsonObject batteryJson = batteriesJson[i].toObject();
            static const QList<JsonParsing::KeyValidateInfo> batteryKeys = {
                {kBatteryCellCountKey, QJsonValue::Double, false},
                {kBatteryCapacityMahKey, QJsonValue::Double, false},
            };
            QString batteryErrorString;
            if (!JsonParsing::validateKeys(batteryJson, batteryKeys, batteryErrorString)) {
                errorString = QObject::tr("batteries[%1]: %2").arg(i).arg(batteryErrorString);
                return false;
            }
            Battery battery;
            battery.cellCount = batteryJson.value(kBatteryCellCountKey).toInt(0);
            battery.capacityMah = batteryJson.value(kBatteryCapacityMahKey).toInt(0);
            batteries.append(battery);
        }
    }

    QList<Sensor> sensors;
    if (json.contains(kSensorsKey)) {
        const QJsonArray sensorsJson = json[kSensorsKey].toArray();
        sensors.reserve(sensorsJson.size());
        for (qsizetype i = 0; i < sensorsJson.size(); ++i) {
            if (!sensorsJson[i].isObject()) {
                errorString = QObject::tr("sensors[%1] is not an object").arg(i);
                return false;
            }
            const QJsonObject sensorJson = sensorsJson[i].toObject();
            static const QList<JsonParsing::KeyValidateInfo> sensorKeys = {
                {kSensorTypeKey, QJsonValue::String, true},
                {kSensorModelKey, QJsonValue::String, false},
            };
            QString sensorErrorString;
            if (!JsonParsing::validateKeys(sensorJson, sensorKeys, sensorErrorString)) {
                errorString = QObject::tr("sensors[%1]: %2").arg(i).arg(sensorErrorString);
                return false;
            }
            Sensor sensor;
            sensor.type = sensorJson[kSensorTypeKey].toString();
            sensor.model = sensorJson.value(kSensorModelKey).toString();
            sensors.append(sensor);
        }
    }

    QString fcHardware;
    QGCMAVLinkTypes::FirmwareClass_t parsedFirmware = 0;  // MAV_AUTOPILOT_GENERIC
    QString fcFirmwareVersion;
    if (json.contains(kFlightControllerKey)) {
        const QJsonObject fcJson = json[kFlightControllerKey].toObject();
        static const QList<JsonParsing::KeyValidateInfo> fcKeys = {
            {kFCHardwareKey, QJsonValue::String, false},
            {kFCFirmwareKey, QJsonValue::String, false},
            {kFCFirmwareVersionKey, QJsonValue::String, false},
        };
        QString fcErrorString;
        if (!JsonParsing::validateKeys(fcJson, fcKeys, fcErrorString)) {
            errorString = QObject::tr("flightController: %1").arg(fcErrorString);
            return false;
        }
        fcHardware = fcJson.value(kFCHardwareKey).toString();
        fcFirmwareVersion = fcJson.value(kFCFirmwareVersionKey).toString();
        if (fcJson.contains(kFCFirmwareKey)) {
            const QString firmwareStr = fcJson[kFCFirmwareKey].toString();
            if (!firmwareFromEnumName(firmwareStr, parsedFirmware)) {
                errorString = QObject::tr("Unknown flightController.firmware value: %1").arg(firmwareStr);
                return false;
            }
        }
    }

    Image image;
    if (json.contains(kImageKey)) {
        const QJsonObject imageJson = json[kImageKey].toObject();
        static const QList<JsonParsing::KeyValidateInfo> imageKeys = {
            {kImageMimeTypeKey, QJsonValue::String, true},
            {kImageDataKey, QJsonValue::String, true},
        };
        QString imageErrorString;
        if (!JsonParsing::validateKeys(imageJson, imageKeys, imageErrorString)) {
            errorString = QObject::tr("image: %1").arg(imageErrorString);
            return false;
        }
        image.mimeType = imageJson[kImageMimeTypeKey].toString();
        image.data = QByteArray::fromBase64(imageJson[kImageDataKey].toString().toLatin1());
    }

    // Every check above passed - commit the parsed values together so a failed load never
    // leaves this profile half-updated.
    _id = json[kIdKey].toString();
    _name = json[kNameKey].toString();
    _manufacturer = json.value(kManufacturerKey).toString();
    _model = json.value(kModelKey).toString();
    _active = parsedActive;
    _mavType = parsedMavType;
    _weightKg = json.value(kWeightKgKey).toDouble(0.0);
    _maxPayloadKg = json.value(kMaxPayloadKgKey).toDouble(0.0);
    _maxFlightTimeMinutes = json.value(kMaxFlightTimeMinutesKey).toInt(0);
    _registrationNumber = json.value(kRegistrationNumberKey).toString();
    _serialNumber = json.value(kSerialNumberKey).toString();
    _batteries = batteries;
    _flightController.hardware = fcHardware;
    _flightController.firmware = parsedFirmware;
    _flightController.firmwareVersion = fcFirmwareVersion;
    _sensors = sensors;
    _notes = json.value(kNotesKey).toString();
    _image = image;

    return true;
}

QByteArray VehicleProfile::toJson() const
{
    QJsonObject json;
    JsonParsing::saveQGCJsonFileHeader(json, kFileType, kFileVersion);

    json[kIdKey] = _id;
    json[kNameKey] = _name;
    json[kManufacturerKey] = _manufacturer;
    json[kModelKey] = _model;
    json[kStatusKey] = _active ? QLatin1String(kStatusActiveValue) : QLatin1String(kStatusInactiveValue);
    json[kMavTypeKey] = mavTypeToEnumName(_mavType);
    json[kWeightKgKey] = _weightKg;
    json[kMaxPayloadKgKey] = _maxPayloadKg;
    json[kMaxFlightTimeMinutesKey] = _maxFlightTimeMinutes;
    json[kRegistrationNumberKey] = _registrationNumber;
    json[kSerialNumberKey] = _serialNumber;

    QJsonArray batteriesJson;
    for (const Battery& battery : _batteries) {
        QJsonObject batteryJson;
        batteryJson[kBatteryCellCountKey] = battery.cellCount;
        batteryJson[kBatteryCapacityMahKey] = battery.capacityMah;
        batteriesJson.append(batteryJson);
    }
    json[kBatteriesKey] = batteriesJson;

    QJsonArray sensorsJson;
    for (const Sensor& sensor : _sensors) {
        QJsonObject sensorJson;
        sensorJson[kSensorTypeKey] = sensor.type;
        sensorJson[kSensorModelKey] = sensor.model;
        sensorsJson.append(sensorJson);
    }
    json[kSensorsKey] = sensorsJson;

    QJsonObject fcJson;
    fcJson[kFCHardwareKey] = _flightController.hardware;
    fcJson[kFCFirmwareKey] = firmwareToEnumName(_flightController.firmware);
    fcJson[kFCFirmwareVersionKey] = _flightController.firmwareVersion;
    json[kFlightControllerKey] = fcJson;

    json[kNotesKey] = _notes;

    if (_image.isValid()) {
        QJsonObject imageJson;
        imageJson[kImageMimeTypeKey] = _image.mimeType;
        imageJson[kImageDataKey] = QString::fromLatin1(_image.data.toBase64());
        json[kImageKey] = imageJson;
    }

    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

bool VehicleProfile::importImage(const QByteArray& fileBytes, QString& errorString)
{
    QBuffer buffer;
    buffer.setData(fileBytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        errorString = QObject::tr("Unable to open image data");
        return false;
    }

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QByteArray sourceFormat = reader.format();
    const QImage sourceImage = reader.read();
    if (sourceImage.isNull()) {
        errorString = QObject::tr("Unable to read image: %1").arg(reader.errorString());
        return false;
    }

    const int longEdge = qMax(sourceImage.width(), sourceImage.height());
    if (longEdge <= kKeepOriginalMaxLongEdgePx && fileBytes.size() <= kKeepOriginalMaxBytes) {
        _image.mimeType = mimeTypeForImageReaderFormat(sourceFormat);
        _image.data = fileBytes;
        return true;
    }

    const QImage scaledImage =
        sourceImage.scaled(kScaledLongEdgePx, kScaledLongEdgePx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const bool hasAlpha = scaledImage.hasAlphaChannel();

    QByteArray encoded;
    QBuffer outBuffer(&encoded);
    outBuffer.open(QIODevice::WriteOnly);
    const bool saved =
        hasAlpha ? scaledImage.save(&outBuffer, "PNG") : scaledImage.save(&outBuffer, "JPG", kScaledJpegQuality);
    outBuffer.close();
    if (!saved) {
        errorString = QObject::tr("Unable to encode scaled image");
        return false;
    }

    _image.mimeType = hasAlpha ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
    _image.data = encoded;
    return true;
}
