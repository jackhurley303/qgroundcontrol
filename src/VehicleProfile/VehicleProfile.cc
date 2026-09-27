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
constexpr const char* kVehicleClassKey = "vehicleClass";
constexpr const char* kAirframeKey = "airframe";
constexpr const char* kLengthMKey = "lengthM";
constexpr const char* kWidthMKey = "widthM";
constexpr const char* kHeightMKey = "heightM";
constexpr const char* kWeightKgKey = "weightKg";
constexpr const char* kSensorsKey = "sensors";
constexpr const char* kSensorTypeKey = "type";
constexpr const char* kSensorModelKey = "model";
constexpr const char* kSensorNotesKey = "notes";
constexpr const char* kFlightControllerKey = "flightController";
constexpr const char* kFCHardwareKey = "hardware";
constexpr const char* kFCFirmwareKey = "firmware";
constexpr const char* kFCFirmwareVersionKey = "firmwareVersion";
constexpr const char* kNotesKey = "notes";
constexpr const char* kImageKey = "image";
constexpr const char* kImageMimeTypeKey = "mimeType";
constexpr const char* kImageDataKey = "data";

/// QGCMAVLink has vehicleClassToInternalString() but no inverse - this is that inverse.
/// QGCMAVLink::allVehicleClasses() is not the right source list here: it holds only the six
/// classes QGC's vehicle-setup UI offers a user to pick from, and omits VehicleClassAirship
/// and VehicleClassSpacecraft. vehicleClassToInternalString() names all eight, so a profile
/// saved with either omitted class would write correctly and then fail to load. This list is
/// instead every class that function names, in the same order as its switch statement, so a
/// reader can compare the two by eye.
bool vehicleClassFromInternalString(const QString& value, QGCMAVLinkTypes::VehicleClass_t& vehicleClass)
{
    static const QList<QGCMAVLink::VehicleClass_t> candidates = {
        QGCMAVLink::VehicleClassAirship, QGCMAVLink::VehicleClassFixedWing,  QGCMAVLink::VehicleClassRoverBoat,
        QGCMAVLink::VehicleClassSub,     QGCMAVLink::VehicleClassSpacecraft, QGCMAVLink::VehicleClassMultiRotor,
        QGCMAVLink::VehicleClassVTOL,    QGCMAVLink::VehicleClassGeneric,
    };
    for (QGCMAVLink::VehicleClass_t candidate : candidates) {
        if (QGCMAVLink::vehicleClassToInternalString(candidate) == value) {
            vehicleClass = candidate;
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
        {kIdKey, QJsonValue::String, true},           {kNameKey, QJsonValue::String, true},
        {kVehicleClassKey, QJsonValue::String, true}, {kAirframeKey, QJsonValue::Object, true},
        {kSensorsKey, QJsonValue::Array, false},      {kFlightControllerKey, QJsonValue::Object, false},
        {kNotesKey, QJsonValue::String, false},       {kImageKey, QJsonValue::Object, false},
    };
    if (!JsonParsing::validateKeys(json, rootKeys, errorString)) {
        return false;
    }

    QGCMAVLinkTypes::VehicleClass_t parsedVehicleClass = QGCMAVLinkTypes::VehicleClassGeneric;
    const QString vehicleClassStr = json[kVehicleClassKey].toString();
    if (!vehicleClassFromInternalString(vehicleClassStr, parsedVehicleClass)) {
        errorString = QObject::tr("Unknown vehicleClass value: %1").arg(vehicleClassStr);
        return false;
    }

    const QJsonObject airframeJson = json[kAirframeKey].toObject();
    Airframe airframe;
    airframe.lengthM = airframeJson.value(kLengthMKey).toDouble(0.0);
    airframe.widthM = airframeJson.value(kWidthMKey).toDouble(0.0);
    airframe.heightM = airframeJson.value(kHeightMKey).toDouble(0.0);
    airframe.weightKg = airframeJson.value(kWeightKgKey).toDouble(0.0);

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
                {kSensorNotesKey, QJsonValue::String, false},
            };
            QString sensorErrorString;
            if (!JsonParsing::validateKeys(sensorJson, sensorKeys, sensorErrorString)) {
                errorString = QObject::tr("sensors[%1]: %2").arg(i).arg(sensorErrorString);
                return false;
            }
            Sensor sensor;
            sensor.type = sensorJson[kSensorTypeKey].toString();
            sensor.model = sensorJson.value(kSensorModelKey).toString();
            sensor.notes = sensorJson.value(kSensorNotesKey).toString();
            sensors.append(sensor);
        }
    }

    FlightController flightController;
    if (json.contains(kFlightControllerKey)) {
        const QJsonObject fcJson = json[kFlightControllerKey].toObject();
        flightController.hardware = fcJson.value(kFCHardwareKey).toString();
        flightController.firmware = fcJson.value(kFCFirmwareKey).toString();
        flightController.firmwareVersion = fcJson.value(kFCFirmwareVersionKey).toString();
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
    _vehicleClass = parsedVehicleClass;
    _airframe = airframe;
    _sensors = sensors;
    _flightController = flightController;
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
    json[kVehicleClassKey] = QGCMAVLink::vehicleClassToInternalString(_vehicleClass);

    QJsonObject airframeJson;
    airframeJson[kLengthMKey] = _airframe.lengthM;
    airframeJson[kWidthMKey] = _airframe.widthM;
    airframeJson[kHeightMKey] = _airframe.heightM;
    airframeJson[kWeightKgKey] = _airframe.weightKg;
    json[kAirframeKey] = airframeJson;

    QJsonArray sensorsJson;
    for (const Sensor& sensor : _sensors) {
        QJsonObject sensorJson;
        sensorJson[kSensorTypeKey] = sensor.type;
        sensorJson[kSensorModelKey] = sensor.model;
        sensorJson[kSensorNotesKey] = sensor.notes;
        sensorsJson.append(sensorJson);
    }
    json[kSensorsKey] = sensorsJson;

    QJsonObject fcJson;
    fcJson[kFCHardwareKey] = _flightController.hardware;
    fcJson[kFCFirmwareKey] = _flightController.firmware;
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
