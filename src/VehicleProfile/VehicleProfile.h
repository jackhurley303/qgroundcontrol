#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>

#include "QGCMAVLinkTypes.h"

/// One vehicle profile: the fields a user records about a vehicle they own, read from and
/// written to a single `.vehicle` JSON file (version 1 of the format). This is a static
/// description the user edits offline - never the live, connected vehicle, which is the
/// `Vehicle` class (src/Vehicle/Vehicle.h). The name says VehicleProfile and the UI says
/// "Vehicles" to keep the two apart.
///
/// Owns the file format only: parsing, serializing, and the image import rule. Loading
/// every file in a folder, creating, saving to disk and deleting belong to
/// VehicleProfileManager.
class VehicleProfile
{
public:
    VehicleProfile();

    /// The `fileType` value every `.vehicle` file carries, and the only `version` this
    /// class currently reads or writes.
    static constexpr const char* kFileType = "Vehicle";
    static constexpr int kFileVersion = 1;

    /// The maximum long edge, in pixels, at which an imported image's original bytes are
    /// kept unchanged. A larger image is scaled down to this size.
    static constexpr int kKeepOriginalMaxLongEdgePx = 2048;
    /// The maximum file size, in bytes, at which an imported image's original bytes are
    /// kept unchanged.
    static constexpr qint64 kKeepOriginalMaxBytes = 2 * 1024 * 1024;
    /// The long edge, in pixels, a scaled-down image is resized to.
    static constexpr int kScaledLongEdgePx = 2048;
    /// The JPEG quality (0-100) used when a scaled-down image has no transparency.
    static constexpr int kScaledJpegQuality = 90;

    /// One entry in the optional `sensors` array.
    struct Sensor
    {
        QString type;   ///< required, e.g. "camera"
        QString model;  ///< optional

        bool operator==(const Sensor&) const = default;
    };

    /// One entry in the optional `batteries` array: the packs the vehicle carries in one
    /// flight. Both fields are optional and default to 0 when absent.
    struct Battery
    {
        int cellCount = 0;
        int capacityMah = 0;

        bool operator==(const Battery&) const = default;
    };

    /// The optional `flightController` object: hardware, firmware and firmware version.
    /// `hardware` and `firmwareVersion` are free text, defaulting to an empty string when
    /// absent. `firmware` is a MAV_AUTOPILOT value for one of the three FirmwareClass_t
    /// values (QGCMAVLinkTypes::FirmwareClass_t rather than the MAV_AUTOPILOT enum itself, to
    /// keep this header light - see mavType() below); it defaults to 0 (MAV_AUTOPILOT_GENERIC)
    /// when absent.
    struct FlightController
    {
        QString hardware;
        QGCMAVLinkTypes::FirmwareClass_t firmware = 0;
        QString firmwareVersion;
    };

    /// The optional `image` object. `isValid()` is false when the profile has no image.
    struct Image
    {
        QString mimeType;  ///< "image/jpeg" or "image/png"
        QByteArray data;   ///< decoded image bytes - never base64 outside the JSON file

        bool isValid() const { return !mimeType.isEmpty() && !data.isEmpty(); }
    };

    /// Parses `bytes` as a `.vehicle` file. Returns false and fills `errorString` on a
    /// broken-JSON file, a `fileType` other than "Vehicle", a `version` this class does
    /// not support, a missing required field, an unknown or non-vehicle `mavType`, an
    /// unknown `status`, or an unknown `flightController.firmware`. Never asserts - a bad
    /// file is always reported, not crashed on.
    bool loadJson(const QByteArray& bytes, QString& errorString);

    /// Serializes this profile to a version-1 `.vehicle` file, compact JSON.
    QByteArray toJson() const;

    /// Applies the image import rule to `fileBytes`, the raw bytes of an image file in any
    /// format the platform's Qt image plugins can read (PNG, JPEG, BMP and GIF at least).
    /// Keeps the original bytes unchanged when the long edge is at most
    /// kKeepOriginalMaxLongEdgePx and the file is at most kKeepOriginalMaxBytes; otherwise
    /// scales to kScaledLongEdgePx on the long edge and saves as JPEG at
    /// kScaledJpegQuality, or as PNG if the image has transparency. Returns false and
    /// fills `errorString` if `fileBytes` is not a readable image.
    bool importImage(const QByteArray& fileBytes, QString& errorString);

    /// Removes the image, if any.
    void clearImage() { _image = Image(); }

    /// Every `mavType` the file format allows: the vehicle types
    /// QGCMAVLink::mavTypeToString() names, excluding the non-vehicle types (ground station,
    /// antenna tracker, onboard controller, gimbal, ADSB, and the reserved VTOL slot). The
    /// file stores one of these values by its MAVLink enum name, e.g. "MAV_TYPE_HEXAROTOR",
    /// never mavTypeToString()'s translated display text.
    static const QList<QGCMAVLinkTypes::VehicleClass_t>& allowedMavTypes();

    /// The three MAV_AUTOPILOT values `flightController.firmware` can hold - the
    /// FirmwareClass_t values QGCMAVLink::firmwareClassToString() names. The file stores one
    /// of these by its MAVLink enum name, e.g. "MAV_AUTOPILOT_PX4".
    static const QList<QGCMAVLinkTypes::FirmwareClass_t>& allowedFirmwareTypes();

    /// Stable identifier set at construction and never changed by this class. External tools
    /// that link a vehicle across devices depend on it staying the same for its lifetime.
    QString id() const { return _id; }

    QString name() const { return _name; }

    void setName(const QString& name) { _name = name; }

    QString manufacturer() const { return _manufacturer; }

    void setManufacturer(const QString& manufacturer) { _manufacturer = manufacturer; }

    QString model() const { return _model; }

    void setModel(const QString& model) { _model = model; }

    /// True for "active", false for "inactive" - the only two values the `status` key
    /// accepts. Defaults to true (active) when the key is absent.
    bool active() const { return _active; }

    void setActive(bool active) { _active = active; }

    /// The vehicle's MAVLink type. QGCMAVLinkTypes::VehicleClass_t (a plain int, not the
    /// MAV_TYPE enum itself) so this header stays out of the MAVLink enum headers - the same
    /// trick QGCMAVLink.h already uses for its coarser VehicleClass_t categories.
    QGCMAVLinkTypes::VehicleClass_t mavType() const { return _mavType; }

    void setMavType(QGCMAVLinkTypes::VehicleClass_t mavType) { _mavType = mavType; }

    double weightKg() const { return _weightKg; }

    void setWeightKg(double weightKg) { _weightKg = weightKg; }

    double maxPayloadKg() const { return _maxPayloadKg; }

    void setMaxPayloadKg(double maxPayloadKg) { _maxPayloadKg = maxPayloadKg; }

    /// Flight time with no payload, in whole minutes.
    int maxFlightTimeMinutes() const { return _maxFlightTimeMinutes; }

    void setMaxFlightTimeMinutes(int maxFlightTimeMinutes) { _maxFlightTimeMinutes = maxFlightTimeMinutes; }

    QString registrationNumber() const { return _registrationNumber; }

    void setRegistrationNumber(const QString& registrationNumber) { _registrationNumber = registrationNumber; }

    QString serialNumber() const { return _serialNumber; }

    void setSerialNumber(const QString& serialNumber) { _serialNumber = serialNumber; }

    QList<Battery> batteries() const { return _batteries; }

    void setBatteries(const QList<Battery>& batteries) { _batteries = batteries; }

    QList<Sensor> sensors() const { return _sensors; }

    void setSensors(const QList<Sensor>& sensors) { _sensors = sensors; }

    FlightController flightController() const { return _flightController; }

    void setFlightController(const FlightController& flightController) { _flightController = flightController; }

    QString notes() const { return _notes; }

    void setNotes(const QString& notes) { _notes = notes; }

    Image image() const { return _image; }

private:
    QString _id;
    QString _name;
    QString _manufacturer;
    QString _model;
    bool _active = true;
    QGCMAVLinkTypes::VehicleClass_t _mavType = QGCMAVLinkTypes::VehicleClassGeneric;
    double _weightKg = 0.0;
    double _maxPayloadKg = 0.0;
    int _maxFlightTimeMinutes = 0;
    QString _registrationNumber;
    QString _serialNumber;
    QList<Battery> _batteries;
    FlightController _flightController;
    QList<Sensor> _sensors;
    QString _notes;
    Image _image;
};
