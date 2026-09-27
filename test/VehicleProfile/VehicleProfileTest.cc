#include "VehicleProfileTest.h"

#include <QtCore/QBuffer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QImageReader>

#include "QGCMAVLink.h"
#include "VehicleProfile.h"

UT_REGISTER_TEST_LIGHTWEIGHT(VehicleProfileTest, TestLabel::Unit, TestLabel::Utilities)

QByteArray VehicleProfileTest::_makeTestImageBytes(int width, int height, QImage::Format format, QColor color)
{
    QImage image(width, height, format);
    image.fill(color);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    const bool saved = image.save(&buffer, "PNG");
    Q_ASSERT(saved);
    buffer.close();
    return bytes;
}

void VehicleProfileTest::_fullRoundTrip()
{
    VehicleProfile profile;
    profile.setName(QStringLiteral("Survey Quad"));
    profile.setManufacturer(QStringLiteral("DJI"));
    profile.setModel(QStringLiteral("Matrice 350 RTK"));
    profile.setActive(false);
    profile.setMavType(MAV_TYPE_QUADROTOR);
    profile.setWeightKg(1.4);
    profile.setMaxPayloadKg(0.3);
    profile.setMaxFlightTimeMinutes(32);
    profile.setRegistrationNumber(QStringLiteral("N12345"));
    profile.setSerialNumber(QStringLiteral("SN-0042"));

    VehicleProfile::Battery batteryA;
    batteryA.cellCount = 6;
    batteryA.capacityMah = 10000;
    VehicleProfile::Battery batteryB;
    batteryB.cellCount = 4;
    batteryB.capacityMah = 5000;
    profile.setBatteries({batteryA, batteryB});

    VehicleProfile::Sensor sensor;
    sensor.type = QStringLiteral("camera");
    sensor.model = QStringLiteral("Sony RX1");
    profile.setSensors({sensor});

    VehicleProfile::FlightController flightController;
    flightController.hardware = QStringLiteral("Pixhawk 6C");
    flightController.firmware = QGCMAVLink::FirmwareClassPX4;
    flightController.firmwareVersion = QStringLiteral("1.15.0");
    profile.setFlightController(flightController);

    profile.setNotes(QStringLiteral("Primary mapping vehicle"));

    QString importErrorString;
    QVERIFY2(profile.importImage(_makeTestImageBytes(32, 32, QImage::Format_RGB32, Qt::red), importErrorString),
             qPrintable(importErrorString));

    const QByteArray bytes = profile.toJson();

    VehicleProfile loaded;
    QString errorString;
    QVERIFY2(loaded.loadJson(bytes, errorString), qPrintable(errorString));

    QCOMPARE(loaded.id(), profile.id());
    QCOMPARE(loaded.name(), profile.name());
    QCOMPARE(loaded.manufacturer(), profile.manufacturer());
    QCOMPARE(loaded.model(), profile.model());
    QCOMPARE(loaded.active(), profile.active());
    QCOMPARE(loaded.mavType(), profile.mavType());
    QCOMPARE(loaded.weightKg(), profile.weightKg());
    QCOMPARE(loaded.maxPayloadKg(), profile.maxPayloadKg());
    QCOMPARE(loaded.maxFlightTimeMinutes(), profile.maxFlightTimeMinutes());
    QCOMPARE(loaded.registrationNumber(), profile.registrationNumber());
    QCOMPARE(loaded.serialNumber(), profile.serialNumber());

    QCOMPARE(loaded.batteries().size(), 2);
    QCOMPARE(loaded.batteries().at(0).cellCount, batteryA.cellCount);
    QCOMPARE(loaded.batteries().at(0).capacityMah, batteryA.capacityMah);
    QCOMPARE(loaded.batteries().at(1).cellCount, batteryB.cellCount);
    QCOMPARE(loaded.batteries().at(1).capacityMah, batteryB.capacityMah);

    QCOMPARE(loaded.sensors().size(), profile.sensors().size());
    QCOMPARE(loaded.sensors().first().type, profile.sensors().first().type);
    QCOMPARE(loaded.sensors().first().model, profile.sensors().first().model);

    QCOMPARE(loaded.flightController().hardware, profile.flightController().hardware);
    QCOMPARE(loaded.flightController().firmware, profile.flightController().firmware);
    QCOMPARE(loaded.flightController().firmwareVersion, profile.flightController().firmwareVersion);

    QCOMPARE(loaded.notes(), profile.notes());

    QVERIFY(loaded.image().isValid());
    QCOMPARE(loaded.image().mimeType, profile.image().mimeType);
    QCOMPARE(loaded.image().data, profile.image().data);
}

void VehicleProfileTest::_loadRejectsBrokenJson()
{
    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QByteArrayLiteral("{ this is not valid json"), errorString));
    QVERIFY(!errorString.isEmpty());
}

void VehicleProfileTest::_loadRejectsWrongFileType()
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "NotVehicle";
    json["version"] = 1;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    json["name"] = "Test";
    json["mavType"] = "MAV_TYPE_QUADROTOR";

    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString));
    QVERIFY(!errorString.isEmpty());
}

void VehicleProfileTest::_loadRejectsUnsupportedVersion()
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 2;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    json["name"] = "Test";
    json["mavType"] = "MAV_TYPE_QUADROTOR";

    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString));
    QVERIFY2(errorString.contains(QStringLiteral("version"), Qt::CaseInsensitive), qPrintable(errorString));
}

void VehicleProfileTest::_loadRejectsMissingRequiredField()
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 1;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    // "name" is deliberately omitted - it is required.
    json["mavType"] = "MAV_TYPE_QUADROTOR";

    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString));
    QVERIFY(!errorString.isEmpty());
}

void VehicleProfileTest::_loadAcceptsMissingOptionalField()
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 1;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    json["name"] = "Test";
    json["mavType"] = "MAV_TYPE_QUADROTOR";
    // "manufacturer", "model", "status", "weightKg", "maxPayloadKg",
    // "maxFlightTimeMinutes", "registrationNumber", "serialNumber", "batteries",
    // "flightController", "sensors", "notes" and "image" are all optional and deliberately
    // omitted here.

    VehicleProfile profile;
    QString errorString;
    QVERIFY2(profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString),
             qPrintable(errorString));

    QVERIFY(profile.manufacturer().isEmpty());
    QVERIFY(profile.model().isEmpty());
    QCOMPARE(profile.active(), true);  // missing status reads as "active"
    QCOMPARE(profile.weightKg(), 0.0);
    QCOMPARE(profile.maxPayloadKg(), 0.0);
    QCOMPARE(profile.maxFlightTimeMinutes(), 0);
    QVERIFY(profile.registrationNumber().isEmpty());
    QVERIFY(profile.serialNumber().isEmpty());
    QVERIFY(profile.batteries().isEmpty());
    QVERIFY(profile.sensors().isEmpty());
    QVERIFY(profile.flightController().hardware.isEmpty());
    QCOMPARE(profile.flightController().firmware, QGCMAVLink::FirmwareClassGeneric);
    QVERIFY(profile.flightController().firmwareVersion.isEmpty());
    QVERIFY(profile.notes().isEmpty());
    QVERIFY(!profile.image().isValid());
}

void VehicleProfileTest::_loadRejectsUnknownMavType()
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 1;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    json["name"] = "Test";
    json["mavType"] = "MAV_TYPE_TELEPORTER";

    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString));
    QVERIFY(!errorString.isEmpty());
}

void VehicleProfileTest::_loadRejectsNonVehicleMavType()
{
    // MAV_TYPE_GCS is a real MAV_TYPE, but not a vehicle - the file format must reject it the
    // same way it rejects a made-up string.
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 1;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    json["name"] = "Test";
    json["mavType"] = "MAV_TYPE_GCS";

    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString));
    QVERIFY(!errorString.isEmpty());
}

void VehicleProfileTest::_mavTypeRoundTripsEveryAllowedType()
{
    for (QGCMAVLinkTypes::VehicleClass_t mavType : VehicleProfile::allowedMavTypes()) {
        VehicleProfile profile;
        profile.setName(QStringLiteral("MavType Round Trip"));
        profile.setMavType(mavType);

        const QByteArray bytes = profile.toJson();

        VehicleProfile loaded;
        QString errorString;
        QVERIFY2(loaded.loadJson(bytes, errorString),
                 qPrintable(QStringLiteral("mavType %1: %2").arg(mavType).arg(errorString)));
        QCOMPARE(loaded.mavType(), mavType);
    }
}

void VehicleProfileTest::_loadRejectsUnknownStatus()
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 1;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    json["name"] = "Test";
    json["mavType"] = "MAV_TYPE_QUADROTOR";
    json["status"] = "retired";

    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString));
    QVERIFY(!errorString.isEmpty());
}

void VehicleProfileTest::_loadRejectsUnknownFirmware()
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 1;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    json["name"] = "Test";
    json["mavType"] = "MAV_TYPE_QUADROTOR";
    QJsonObject flightController;
    flightController["firmware"] = "MAV_AUTOPILOT_INVENTED";
    json["flightController"] = flightController;

    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString));
    QVERIFY(!errorString.isEmpty());
}

void VehicleProfileTest::_firmwareRoundTripsEveryAllowedType()
{
    for (QGCMAVLinkTypes::FirmwareClass_t firmware : VehicleProfile::allowedFirmwareTypes()) {
        VehicleProfile profile;
        profile.setName(QStringLiteral("Firmware Round Trip"));
        profile.setMavType(MAV_TYPE_QUADROTOR);

        VehicleProfile::FlightController flightController;
        flightController.firmware = firmware;
        profile.setFlightController(flightController);

        const QByteArray bytes = profile.toJson();

        VehicleProfile loaded;
        QString errorString;
        QVERIFY2(loaded.loadJson(bytes, errorString),
                 qPrintable(QStringLiteral("firmware %1: %2").arg(firmware).arg(errorString)));
        QCOMPARE(loaded.flightController().firmware, firmware);
    }
}

void VehicleProfileTest::_dualBatteryRoundTrips()
{
    VehicleProfile profile;
    profile.setName(QStringLiteral("Dual Battery"));
    profile.setMavType(MAV_TYPE_HEXAROTOR);

    VehicleProfile::Battery first;
    first.cellCount = 6;
    first.capacityMah = 10000;
    VehicleProfile::Battery second;
    second.cellCount = 6;
    second.capacityMah = 10000;
    profile.setBatteries({first, second});

    VehicleProfile loaded;
    QString errorString;
    QVERIFY2(loaded.loadJson(profile.toJson(), errorString), qPrintable(errorString));
    QCOMPARE(loaded.batteries().size(), 2);
    QCOMPARE(loaded.batteries(), profile.batteries());
}

void VehicleProfileTest::_sensorWithoutModelRoundTrips()
{
    VehicleProfile profile;
    profile.setName(QStringLiteral("Bare Sensor"));
    profile.setMavType(MAV_TYPE_QUADROTOR);

    VehicleProfile::Sensor sensor;
    sensor.type = QStringLiteral("gps");
    // "model" is deliberately left empty - it is optional.
    profile.setSensors({sensor});

    VehicleProfile loaded;
    QString errorString;
    QVERIFY2(loaded.loadJson(profile.toJson(), errorString), qPrintable(errorString));
    QCOMPARE(loaded.sensors().size(), 1);
    QCOMPARE(loaded.sensors().first().type, QStringLiteral("gps"));
    QVERIFY(loaded.sensors().first().model.isEmpty());
}

void VehicleProfileTest::_importImageKeepsSmallImageByteForByte()
{
    const QByteArray sourceBytes = _makeTestImageBytes(32, 32, QImage::Format_RGB32, Qt::blue);

    VehicleProfile profile;
    QString errorString;
    QVERIFY2(profile.importImage(sourceBytes, errorString), qPrintable(errorString));

    QCOMPARE(profile.image().mimeType, QStringLiteral("image/png"));
    QCOMPARE(profile.image().data, sourceBytes);
}

void VehicleProfileTest::_importImageScalesLargeImageToLongEdge()
{
    // 3000x1500, no alpha channel: over the 2048px long-edge limit, so this must scale down
    // and (having no transparency) save as JPEG.
    const QByteArray sourceBytes = _makeTestImageBytes(3000, 1500, QImage::Format_RGB32, Qt::green);

    VehicleProfile profile;
    QString errorString;
    QVERIFY2(profile.importImage(sourceBytes, errorString), qPrintable(errorString));

    QCOMPARE(profile.image().mimeType, QStringLiteral("image/jpeg"));
    QVERIFY(profile.image().data != sourceBytes);

    QBuffer resultBuffer;
    resultBuffer.setData(profile.image().data);
    resultBuffer.open(QIODevice::ReadOnly);
    QImageReader reader(&resultBuffer);
    const QImage resultImage = reader.read();
    QVERIFY2(!resultImage.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(qMax(resultImage.width(), resultImage.height()), VehicleProfile::kScaledLongEdgePx);
}

void VehicleProfileTest::_importImageScaledWithTransparencySavesAsPng()
{
    // 3000x1500 with a non-opaque alpha channel: over the size limit, so this scales down,
    // and (having transparency) must save as PNG rather than JPEG.
    QImage sourceImage(3000, 1500, QImage::Format_ARGB32);
    sourceImage.fill(QColor(255, 0, 0, 128));

    QByteArray sourceBytes;
    QBuffer sourceBuffer(&sourceBytes);
    sourceBuffer.open(QIODevice::WriteOnly);
    QVERIFY(sourceImage.save(&sourceBuffer, "PNG"));
    sourceBuffer.close();

    VehicleProfile profile;
    QString errorString;
    QVERIFY2(profile.importImage(sourceBytes, errorString), qPrintable(errorString));

    QCOMPARE(profile.image().mimeType, QStringLiteral("image/png"));

    QBuffer resultBuffer;
    resultBuffer.setData(profile.image().data);
    resultBuffer.open(QIODevice::ReadOnly);
    QImageReader reader(&resultBuffer);
    const QImage resultImage = reader.read();
    QVERIFY2(!resultImage.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(qMax(resultImage.width(), resultImage.height()), VehicleProfile::kScaledLongEdgePx);
    QVERIFY(resultImage.hasAlphaChannel());
}

void VehicleProfileTest::_importImageIndexedWithTransparencySavesAsPng()
{
    // 3000x1500 palette (Format_Indexed8) source whose color table holds a transparent entry,
    // every pixel set to that entry. Over the size limit, so this must scale down and (having
    // transparency, even though it is carried by the palette rather than a per-pixel alpha
    // channel) save as PNG rather than JPEG.
    QImage sourceImage(3000, 1500, QImage::Format_Indexed8);
    QList<QRgb> colorTable;
    colorTable.append(qRgba(255, 0, 0, 255));
    colorTable.append(qRgba(0, 0, 0, 0));
    sourceImage.setColorTable(colorTable);
    sourceImage.fill(1);

    QByteArray sourceBytes;
    QBuffer sourceBuffer(&sourceBytes);
    sourceBuffer.open(QIODevice::WriteOnly);
    QVERIFY(sourceImage.save(&sourceBuffer, "PNG"));
    sourceBuffer.close();

    VehicleProfile profile;
    QString errorString;
    QVERIFY2(profile.importImage(sourceBytes, errorString), qPrintable(errorString));

    QCOMPARE(profile.image().mimeType, QStringLiteral("image/png"));

    QBuffer resultBuffer;
    resultBuffer.setData(profile.image().data);
    resultBuffer.open(QIODevice::ReadOnly);
    QImageReader reader(&resultBuffer);
    const QImage resultImage = reader.read();
    QVERIFY2(!resultImage.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(qMax(resultImage.width(), resultImage.height()), VehicleProfile::kScaledLongEdgePx);
    QVERIFY(resultImage.hasAlphaChannel());
}
