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
    Q_PROPERTY(QString manufacturer READ manufacturer WRITE setManufacturer NOTIFY profileChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY profileChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY profileChanged)
    Q_PROPERTY(int mavType READ mavType WRITE setMavType NOTIFY profileChanged)
    Q_PROPERTY(double weightKg READ weightKg WRITE setWeightKg NOTIFY profileChanged)
    Q_PROPERTY(double maxPayloadKg READ maxPayloadKg WRITE setMaxPayloadKg NOTIFY profileChanged)
    Q_PROPERTY(int maxFlightTimeMinutes READ maxFlightTimeMinutes WRITE setMaxFlightTimeMinutes NOTIFY profileChanged)
    Q_PROPERTY(QString registrationNumber READ registrationNumber WRITE setRegistrationNumber NOTIFY profileChanged)
    Q_PROPERTY(QString serialNumber READ serialNumber WRITE setSerialNumber NOTIFY profileChanged)
    Q_PROPERTY(QVariantList batteries READ batteries WRITE setBatteries NOTIFY profileChanged)
    Q_PROPERTY(QVariantList sensors READ sensors WRITE setSensors NOTIFY profileChanged)
    Q_PROPERTY(QString flightControllerHardware READ flightControllerHardware WRITE setFlightControllerHardware NOTIFY
                   profileChanged)
    Q_PROPERTY(int flightControllerFirmware READ flightControllerFirmware WRITE setFlightControllerFirmware NOTIFY
                   profileChanged)
    Q_PROPERTY(QString flightControllerFirmwareVersion READ flightControllerFirmwareVersion WRITE
                   setFlightControllerFirmwareVersion NOTIFY profileChanged)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY profileChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY profileChanged)
    Q_PROPERTY(QString imageDataUrl READ imageDataUrl NOTIFY profileChanged)

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

    QString manufacturer() const { return _profile.manufacturer(); }

    void setManufacturer(const QString& manufacturer);

    QString model() const { return _profile.model(); }

    void setModel(const QString& model);

    bool active() const { return _profile.active(); }

    void setActive(bool active);

    int mavType() const { return _profile.mavType(); }

    void setMavType(int mavType);

    double weightKg() const { return _profile.weightKg(); }

    void setWeightKg(double weightKg);

    double maxPayloadKg() const { return _profile.maxPayloadKg(); }

    void setMaxPayloadKg(double maxPayloadKg);

    int maxFlightTimeMinutes() const { return _profile.maxFlightTimeMinutes(); }

    void setMaxFlightTimeMinutes(int maxFlightTimeMinutes);

    QString registrationNumber() const { return _profile.registrationNumber(); }

    void setRegistrationNumber(const QString& registrationNumber);

    QString serialNumber() const { return _profile.serialNumber(); }

    void setSerialNumber(const QString& serialNumber);

    /// Each battery is a map with the integer keys `cellCount` and `capacityMah`.
    QVariantList batteries() const;

    void setBatteries(const QVariantList& batteries);

    /// Each sensor is a map with the string keys `type` and `model`.
    QVariantList sensors() const;

    void setSensors(const QVariantList& sensors);

    QString flightControllerHardware() const { return _profile.flightController().hardware; }

    void setFlightControllerHardware(const QString& hardware);

    /// The QGCMAVLinkTypes::FirmwareClass_t value flightController.firmware holds. Fed by
    /// VehicleProfileManager::firmwareTypes() as a picker's `value`.
    int flightControllerFirmware() const { return _profile.flightController().firmware; }

    void setFlightControllerFirmware(int firmware);

    QString flightControllerFirmwareVersion() const { return _profile.flightController().firmwareVersion; }

    void setFlightControllerFirmwareVersion(const QString& firmwareVersion);

    QString notes() const { return _profile.notes(); }

    void setNotes(const QString& notes);

    bool hasImage() const { return _profile.image().isValid(); }

    /// A `data:` URL QML's Image element can load directly, or an empty string if there is no
    /// image. Built fresh on every call from the decoded bytes VehicleProfile holds - the file
    /// itself never stores this string, only mimeType and raw bytes.
    QString imageDataUrl() const;

signals:
    void profileChanged();

private:
    void _setFlightController(const VehicleProfile::FlightController& flightController);

    const QString _filePath;
    VehicleProfile _profile;
};
