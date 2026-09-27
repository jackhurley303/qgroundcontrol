#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

#include "VehicleProfile.h"

Q_DECLARE_LOGGING_CATEGORY(VehicleProfileEntryLog)

/// One vehicle in VehicleProfileManager's list: a VehicleProfile plus the `.vehicle` file it
/// lives in. Exposes every profile field to QML. Edits stay in memory until
/// VehicleProfileManager::saveVehicle() writes them to the file. If the file changes outside
/// QGC first, the reload replaces the unsaved edits.
///
/// The id and the file path are fixed for the life of the object. The file name comes from
/// the vehicle's name at creation and never follows a later rename.
class VehicleProfileEntry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by VehicleProfileManager")

    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString fileName READ fileName CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY profileChanged)
    Q_PROPERTY(int vehicleClass READ vehicleClass WRITE setVehicleClass NOTIFY profileChanged)
    Q_PROPERTY(double lengthM READ lengthM WRITE setLengthM NOTIFY profileChanged)
    Q_PROPERTY(double widthM READ widthM WRITE setWidthM NOTIFY profileChanged)
    Q_PROPERTY(double heightM READ heightM WRITE setHeightM NOTIFY profileChanged)
    Q_PROPERTY(double weightKg READ weightKg WRITE setWeightKg NOTIFY profileChanged)
    Q_PROPERTY(QVariantList sensors READ sensors WRITE setSensors NOTIFY profileChanged)
    Q_PROPERTY(QString flightControllerHardware READ flightControllerHardware WRITE setFlightControllerHardware NOTIFY
                   profileChanged)
    Q_PROPERTY(QString flightControllerFirmware READ flightControllerFirmware WRITE setFlightControllerFirmware NOTIFY
                   profileChanged)
    Q_PROPERTY(QString flightControllerFirmwareVersion READ flightControllerFirmwareVersion WRITE
                   setFlightControllerFirmwareVersion NOTIFY profileChanged)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY profileChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY profileChanged)

public:
    VehicleProfileEntry(const QString& filePath, const VehicleProfile& profile, QObject* parent = nullptr);

    QString filePath() const { return _filePath; }

    const VehicleProfile& profile() const { return _profile; }

    /// Replaces every field with `profile`, for a reload after the file changed on disk.
    /// Returns false and changes nothing if `profile` has a different id, so an entry's id
    /// never changes.
    bool replaceProfile(const VehicleProfile& profile);

    /// Reads the image file at `imagePath` (a local path or a file: URL) and applies the
    /// VehicleProfile image rule. Returns false and logs a warning if the file cannot be read
    /// or is not an image.
    Q_INVOKABLE bool importImage(const QString& imagePath);
    Q_INVOKABLE void clearImage();

    QString id() const { return _profile.id(); }

    QString fileName() const;

    QString name() const { return _profile.name(); }

    void setName(const QString& name);

    int vehicleClass() const { return _profile.vehicleClass(); }

    void setVehicleClass(int vehicleClass);

    double lengthM() const { return _profile.airframe().lengthM; }

    void setLengthM(double lengthM);

    double widthM() const { return _profile.airframe().widthM; }

    void setWidthM(double widthM);

    double heightM() const { return _profile.airframe().heightM; }

    void setHeightM(double heightM);

    double weightKg() const { return _profile.airframe().weightKg; }

    void setWeightKg(double weightKg);

    /// Each sensor is a map with the string keys `type`, `model` and `notes`.
    QVariantList sensors() const;

    void setSensors(const QVariantList& sensors);

    QString flightControllerHardware() const { return _profile.flightController().hardware; }

    void setFlightControllerHardware(const QString& hardware);

    QString flightControllerFirmware() const { return _profile.flightController().firmware; }

    void setFlightControllerFirmware(const QString& firmware);

    QString flightControllerFirmwareVersion() const { return _profile.flightController().firmwareVersion; }

    void setFlightControllerFirmwareVersion(const QString& firmwareVersion);

    QString notes() const { return _profile.notes(); }

    void setNotes(const QString& notes);

    bool hasImage() const { return _profile.image().isValid(); }

signals:
    void profileChanged();

private:
    void _setAirframe(const VehicleProfile::Airframe& airframe);
    void _setFlightController(const VehicleProfile::FlightController& flightController);

    const QString _filePath;
    VehicleProfile _profile;
};
