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
    profile.setVehicleClass(QGCMAVLink::VehicleClassMultiRotor);

    VehicleProfile::Airframe airframe;
    airframe.lengthM = 0.45;
    airframe.widthM = 0.45;
    airframe.heightM = 0.2;
    airframe.weightKg = 1.4;
    profile.setAirframe(airframe);

    VehicleProfile::Sensor sensor;
    sensor.type = QStringLiteral("camera");
    sensor.model = QStringLiteral("Sony RX1");
    sensor.notes = QStringLiteral("Gimbal-mounted");
    profile.setSensors({sensor});

    VehicleProfile::FlightController flightController;
    flightController.hardware = QStringLiteral("Pixhawk 6C");
    flightController.firmware = QStringLiteral("PX4");
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
    QCOMPARE(loaded.vehicleClass(), profile.vehicleClass());

    QCOMPARE(loaded.airframe().lengthM, profile.airframe().lengthM);
    QCOMPARE(loaded.airframe().widthM, profile.airframe().widthM);
    QCOMPARE(loaded.airframe().heightM, profile.airframe().heightM);
    QCOMPARE(loaded.airframe().weightKg, profile.airframe().weightKg);

    QCOMPARE(loaded.sensors().size(), profile.sensors().size());
    QCOMPARE(loaded.sensors().first().type, profile.sensors().first().type);
    QCOMPARE(loaded.sensors().first().model, profile.sensors().first().model);
    QCOMPARE(loaded.sensors().first().notes, profile.sensors().first().notes);

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
    json["vehicleClass"] = "MultiRotor";
    json["airframe"] = QJsonObject();

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
    json["vehicleClass"] = "MultiRotor";
    json["airframe"] = QJsonObject();

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
    json["vehicleClass"] = "MultiRotor";
    json["airframe"] = QJsonObject();

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
    json["vehicleClass"] = "MultiRotor";
    json["airframe"] = QJsonObject();
    // "sensors", "flightController", "notes" and "image" are all optional and deliberately
    // omitted here.

    VehicleProfile profile;
    QString errorString;
    QVERIFY2(profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString),
             qPrintable(errorString));

    QVERIFY(profile.sensors().isEmpty());
    QVERIFY(profile.flightController().hardware.isEmpty());
    QVERIFY(profile.flightController().firmware.isEmpty());
    QVERIFY(profile.notes().isEmpty());
    QVERIFY(!profile.image().isValid());

    // The airframe object itself is required, but every field inside it is optional.
    QCOMPARE(profile.airframe().lengthM, 0.0);
    QCOMPARE(profile.airframe().widthM, 0.0);
    QCOMPARE(profile.airframe().heightM, 0.0);
    QCOMPARE(profile.airframe().weightKg, 0.0);
}

void VehicleProfileTest::_loadRejectsUnknownVehicleClass()
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 1;
    json["id"] = "8f0c2d4e-0000-0000-0000-000000000000";
    json["name"] = "Test";
    json["vehicleClass"] = "Teleporter";
    json["airframe"] = QJsonObject();

    VehicleProfile profile;
    QString errorString;
    QVERIFY(!profile.loadJson(QJsonDocument(json).toJson(QJsonDocument::Compact), errorString));
    QVERIFY(!errorString.isEmpty());
}

void VehicleProfileTest::_vehicleClassRoundTripsEveryClass()
{
    // All eight classes vehicleClassToInternalString() names, not just the six
    // allVehicleClasses() offers a user in the vehicle-setup UI - Airship and Spacecraft are
    // deliberately included since they are the two a prior bug left unable to reload.
    const QList<QGCMAVLink::VehicleClass_t> allClasses = {
        QGCMAVLink::VehicleClassAirship, QGCMAVLink::VehicleClassFixedWing,  QGCMAVLink::VehicleClassRoverBoat,
        QGCMAVLink::VehicleClassSub,     QGCMAVLink::VehicleClassSpacecraft, QGCMAVLink::VehicleClassMultiRotor,
        QGCMAVLink::VehicleClassVTOL,    QGCMAVLink::VehicleClassGeneric,
    };

    for (QGCMAVLink::VehicleClass_t vehicleClass : allClasses) {
        VehicleProfile profile;
        profile.setName(QStringLiteral("Class Round Trip"));
        profile.setVehicleClass(vehicleClass);

        VehicleProfile::Airframe airframe;
        profile.setAirframe(airframe);

        const QByteArray bytes = profile.toJson();

        VehicleProfile loaded;
        QString errorString;
        QVERIFY2(loaded.loadJson(bytes, errorString),
                 qPrintable(QStringLiteral("class %1: %2").arg(vehicleClass).arg(errorString)));
        QCOMPARE(loaded.vehicleClass(), vehicleClass);
    }
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
