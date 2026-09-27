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
        QString notes;  ///< optional
    };

    /// The `airframe` object: size and weight, SI units. Every field is optional and
    /// defaults to 0 when absent.
    struct Airframe
    {
        double lengthM = 0.0;
        double widthM = 0.0;
        double heightM = 0.0;
        double weightKg = 0.0;
    };

    /// The optional `flightController` object: hardware and firmware. Every field
    /// defaults to an empty string when absent.
    struct FlightController
    {
        QString hardware;
        QString firmware;
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
    /// not support, or a missing required field. Never asserts - a bad file is always
    /// reported, not crashed on.
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

    /// Stable identifier set at construction and never changed by this class. QDrive's
    /// snapshot model depends on it staying the same across a vehicle's lifetime.
    QString id() const { return _id; }

    QString name() const { return _name; }

    void setName(const QString& name) { _name = name; }

    QGCMAVLinkTypes::VehicleClass_t vehicleClass() const { return _vehicleClass; }

    void setVehicleClass(QGCMAVLinkTypes::VehicleClass_t vehicleClass) { _vehicleClass = vehicleClass; }

    Airframe airframe() const { return _airframe; }

    void setAirframe(const Airframe& airframe) { _airframe = airframe; }

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
    QGCMAVLinkTypes::VehicleClass_t _vehicleClass = QGCMAVLinkTypes::VehicleClassGeneric;
    Airframe _airframe;
    QList<Sensor> _sensors;
    FlightController _flightController;
    QString _notes;
    Image _image;
};
