#pragma once

#include <QtCore/QByteArray>
#include <QtGui/QColor>
#include <QtGui/QImage>

#include "UnitTest.h"

/// Tests for VehicleProfile: the .vehicle JSON file format, version 1, and the image
/// import rule.
class VehicleProfileTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _fullRoundTrip();
    void _loadRejectsBrokenJson();
    void _loadRejectsWrongFileType();
    void _loadRejectsUnsupportedVersion();
    void _loadRejectsMissingRequiredField();
    void _loadAcceptsMissingOptionalField();
    void _loadRejectsUnknownVehicleClass();
    void _vehicleClassRoundTripsEveryClass();
    void _importImageKeepsSmallImageByteForByte();
    void _importImageScalesLargeImageToLongEdge();
    void _importImageScaledWithTransparencySavesAsPng();
    void _importImageIndexedWithTransparencySavesAsPng();

private:
    /// Encodes a solid-color test image of the given size/format as PNG bytes, as if it
    /// were the raw contents of an image file a user picked to import. Generated in code
    /// rather than checked in, per the plan.
    static QByteArray _makeTestImageBytes(int width, int height, QImage::Format format, QColor color);
};
