#include "VehicleProfileEntry.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(VehicleProfileEntryLog, "VehicleProfile.VehicleProfileEntry")

namespace {

constexpr const char* kSensorTypeKey = "type";
constexpr const char* kSensorModelKey = "model";
constexpr const char* kBatteryCellCountKey = "cellCount";
constexpr const char* kBatteryCapacityMahKey = "capacityMah";

}  // namespace

VehicleProfileEntry::VehicleProfileEntry(const QString& filePath, const VehicleProfile& profile, QObject* parent)
    : QObject(parent)
    , _filePath(filePath)
    , _profile(profile)
{}

bool VehicleProfileEntry::replaceProfile(const VehicleProfile& profile)
{
    if (profile.id() != _profile.id()) {
        qCWarning(VehicleProfileEntryLog)
            << "Refusing to change the id of" << _filePath << "from" << _profile.id() << "to" << profile.id();
        return false;
    }
    _profile = profile;
    emit profileChanged();
    return true;
}

bool VehicleProfileEntry::importImage(const QString& imagePath)
{
    const QUrl url(imagePath);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : imagePath;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(VehicleProfileEntryLog) << "Unable to open image" << localPath << file.errorString();
        return false;
    }

    QString errorString;
    if (!_profile.importImage(file.readAll(), errorString)) {
        qCWarning(VehicleProfileEntryLog) << "Unable to import image" << localPath << errorString;
        return false;
    }
    emit profileChanged();
    return true;
}

void VehicleProfileEntry::clearImage()
{
    if (!_profile.image().isValid()) {
        return;
    }
    _profile.clearImage();
    emit profileChanged();
}

QString VehicleProfileEntry::fileName() const
{
    return QFileInfo(_filePath).fileName();
}

QString VehicleProfileEntry::imageDataUrl() const
{
    const VehicleProfile::Image image = _profile.image();
    if (!image.isValid()) {
        return QString();
    }
    return QStringLiteral("data:%1;base64,%2").arg(image.mimeType, QString::fromLatin1(image.data.toBase64()));
}

void VehicleProfileEntry::setName(const QString& name)
{
    if (name == _profile.name()) {
        return;
    }
    _profile.setName(name);
    emit profileChanged();
}

void VehicleProfileEntry::setManufacturer(const QString& manufacturer)
{
    if (manufacturer == _profile.manufacturer()) {
        return;
    }
    _profile.setManufacturer(manufacturer);
    emit profileChanged();
}

void VehicleProfileEntry::setModel(const QString& model)
{
    if (model == _profile.model()) {
        return;
    }
    _profile.setModel(model);
    emit profileChanged();
}

void VehicleProfileEntry::setActive(bool active)
{
    if (active == _profile.active()) {
        return;
    }
    _profile.setActive(active);
    emit profileChanged();
}

void VehicleProfileEntry::setMavType(int mavType)
{
    if (mavType == _profile.mavType()) {
        return;
    }
    // A type outside this list would save with no enum name, which the file format cannot
    // load back.
    if (!VehicleProfile::allowedMavTypes().contains(mavType)) {
        qCWarning(VehicleProfileEntryLog) << "Unsupported mavType" << mavType;
        return;
    }
    _profile.setMavType(mavType);
    emit profileChanged();
}

void VehicleProfileEntry::setWeightKg(double weightKg)
{
    if (weightKg == _profile.weightKg()) {
        return;
    }
    _profile.setWeightKg(weightKg);
    emit profileChanged();
}

void VehicleProfileEntry::setMaxPayloadKg(double maxPayloadKg)
{
    if (maxPayloadKg == _profile.maxPayloadKg()) {
        return;
    }
    _profile.setMaxPayloadKg(maxPayloadKg);
    emit profileChanged();
}

void VehicleProfileEntry::setMaxFlightTimeMinutes(int maxFlightTimeMinutes)
{
    if (maxFlightTimeMinutes == _profile.maxFlightTimeMinutes()) {
        return;
    }
    _profile.setMaxFlightTimeMinutes(maxFlightTimeMinutes);
    emit profileChanged();
}

void VehicleProfileEntry::setRegistrationNumber(const QString& registrationNumber)
{
    if (registrationNumber == _profile.registrationNumber()) {
        return;
    }
    _profile.setRegistrationNumber(registrationNumber);
    emit profileChanged();
}

void VehicleProfileEntry::setSerialNumber(const QString& serialNumber)
{
    if (serialNumber == _profile.serialNumber()) {
        return;
    }
    _profile.setSerialNumber(serialNumber);
    emit profileChanged();
}

QVariantList VehicleProfileEntry::batteries() const
{
    QVariantList batteries;
    for (const VehicleProfile::Battery& battery : _profile.batteries()) {
        QVariantMap batteryMap;
        batteryMap[kBatteryCellCountKey] = battery.cellCount;
        batteryMap[kBatteryCapacityMahKey] = battery.capacityMah;
        batteries.append(batteryMap);
    }
    return batteries;
}

void VehicleProfileEntry::setBatteries(const QVariantList& batteries)
{
    QList<VehicleProfile::Battery> newBatteries;
    newBatteries.reserve(batteries.size());
    for (const QVariant& batteryVariant : batteries) {
        const QVariantMap batteryMap = batteryVariant.toMap();
        VehicleProfile::Battery battery;
        battery.cellCount = batteryMap.value(kBatteryCellCountKey).toInt();
        battery.capacityMah = batteryMap.value(kBatteryCapacityMahKey).toInt();
        newBatteries.append(battery);
    }
    if (newBatteries == _profile.batteries()) {
        return;
    }
    _profile.setBatteries(newBatteries);
    emit profileChanged();
}

QVariantList VehicleProfileEntry::sensors() const
{
    QVariantList sensors;
    for (const VehicleProfile::Sensor& sensor : _profile.sensors()) {
        QVariantMap sensorMap;
        sensorMap[kSensorTypeKey] = sensor.type;
        sensorMap[kSensorModelKey] = sensor.model;
        sensors.append(sensorMap);
    }
    return sensors;
}

void VehicleProfileEntry::setSensors(const QVariantList& sensors)
{
    QList<VehicleProfile::Sensor> newSensors;
    newSensors.reserve(sensors.size());
    for (const QVariant& sensorVariant : sensors) {
        const QVariantMap sensorMap = sensorVariant.toMap();
        VehicleProfile::Sensor sensor;
        sensor.type = sensorMap.value(kSensorTypeKey).toString();
        sensor.model = sensorMap.value(kSensorModelKey).toString();
        newSensors.append(sensor);
    }
    if (newSensors == _profile.sensors()) {
        return;
    }
    _profile.setSensors(newSensors);
    emit profileChanged();
}

void VehicleProfileEntry::setFlightControllerHardware(const QString& hardware)
{
    VehicleProfile::FlightController flightController = _profile.flightController();
    flightController.hardware = hardware;
    _setFlightController(flightController);
}

void VehicleProfileEntry::setFlightControllerFirmware(int firmware)
{
    if (firmware == _profile.flightController().firmware) {
        return;
    }
    // A value outside this list would save with no enum name, which the file format cannot
    // load back.
    if (!VehicleProfile::allowedFirmwareTypes().contains(firmware)) {
        qCWarning(VehicleProfileEntryLog) << "Unsupported flightController.firmware" << firmware;
        return;
    }
    VehicleProfile::FlightController flightController = _profile.flightController();
    flightController.firmware = firmware;
    _setFlightController(flightController);
}

void VehicleProfileEntry::setFlightControllerFirmwareVersion(const QString& firmwareVersion)
{
    VehicleProfile::FlightController flightController = _profile.flightController();
    flightController.firmwareVersion = firmwareVersion;
    _setFlightController(flightController);
}

void VehicleProfileEntry::setNotes(const QString& notes)
{
    if (notes == _profile.notes()) {
        return;
    }
    _profile.setNotes(notes);
    emit profileChanged();
}

void VehicleProfileEntry::_setFlightController(const VehicleProfile::FlightController& flightController)
{
    const VehicleProfile::FlightController current = _profile.flightController();
    if (flightController.hardware == current.hardware && flightController.firmware == current.firmware &&
        flightController.firmwareVersion == current.firmwareVersion) {
        return;
    }
    _profile.setFlightController(flightController);
    emit profileChanged();
}
