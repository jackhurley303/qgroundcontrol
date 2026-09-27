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
constexpr const char* kSensorNotesKey = "notes";

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

void VehicleProfileEntry::setName(const QString& name)
{
    if (name == _profile.name()) {
        return;
    }
    _profile.setName(name);
    emit profileChanged();
}

void VehicleProfileEntry::setVehicleClass(int vehicleClass)
{
    if (vehicleClass == _profile.vehicleClass()) {
        return;
    }
    // A class outside this list would save as "Unknown", which the file format cannot load.
    if (!VehicleProfile::vehicleClasses().contains(vehicleClass)) {
        qCWarning(VehicleProfileEntryLog) << "Unsupported vehicle class" << vehicleClass;
        return;
    }
    _profile.setVehicleClass(vehicleClass);
    emit profileChanged();
}

void VehicleProfileEntry::setLengthM(double lengthM)
{
    VehicleProfile::Airframe airframe = _profile.airframe();
    airframe.lengthM = lengthM;
    _setAirframe(airframe);
}

void VehicleProfileEntry::setWidthM(double widthM)
{
    VehicleProfile::Airframe airframe = _profile.airframe();
    airframe.widthM = widthM;
    _setAirframe(airframe);
}

void VehicleProfileEntry::setHeightM(double heightM)
{
    VehicleProfile::Airframe airframe = _profile.airframe();
    airframe.heightM = heightM;
    _setAirframe(airframe);
}

void VehicleProfileEntry::setWeightKg(double weightKg)
{
    VehicleProfile::Airframe airframe = _profile.airframe();
    airframe.weightKg = weightKg;
    _setAirframe(airframe);
}

QVariantList VehicleProfileEntry::sensors() const
{
    QVariantList sensors;
    for (const VehicleProfile::Sensor& sensor : _profile.sensors()) {
        QVariantMap sensorMap;
        sensorMap[kSensorTypeKey] = sensor.type;
        sensorMap[kSensorModelKey] = sensor.model;
        sensorMap[kSensorNotesKey] = sensor.notes;
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
        sensor.notes = sensorMap.value(kSensorNotesKey).toString();
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

void VehicleProfileEntry::setFlightControllerFirmware(const QString& firmware)
{
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

void VehicleProfileEntry::_setAirframe(const VehicleProfile::Airframe& airframe)
{
    const VehicleProfile::Airframe current = _profile.airframe();
    if (airframe.lengthM == current.lengthM && airframe.widthM == current.widthM &&
        airframe.heightM == current.heightM && airframe.weightKg == current.weightKg) {
        return;
    }
    _profile.setAirframe(airframe);
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
