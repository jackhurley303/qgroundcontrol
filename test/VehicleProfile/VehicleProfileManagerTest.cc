#include "VehicleProfileManagerTest.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QScopeGuard>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>

#include "AppSettings.h"
#include "QGCFileWatcher.h"
#include "QGCMAVLink.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "VehicleProfile.h"
#include "VehicleProfileEntry.h"
#include "VehicleProfileManager.h"

UT_REGISTER_TEST(VehicleProfileManagerTest, TestLabel::Unit, TestLabel::Utilities)

namespace {

constexpr const char* kManagerLogCategory = "VehicleProfile.VehicleProfileManager";

constexpr const char* kIdA = "8f0c2d4e-0000-0000-0000-00000000000a";
constexpr const char* kIdB = "8f0c2d4e-0000-0000-0000-00000000000b";

VehicleProfileEntry* entryAt(const VehicleProfileManager& manager, int index)
{
    return manager.vehicles()->value<VehicleProfileEntry*>(index);
}

}  // namespace

void VehicleProfileManagerTest::initTestCase()
{
    UnitTest::initTestCase();

    // Some CI machines deliver no file notifications. Probe once, as QGCFileWatcherTest's
    // tests do, so the watcher tests skip there and fail on a miss everywhere else. The
    // timeout stays well under the ctest limit on CI too, and QTest::qWaitFor logs nothing
    // on timeout, unlike UnitTest::waitForCondition, whose warning strict log mode rejects.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir dir(tempDir.path());
    const QString watchedFile = dir.filePath(QStringLiteral("watched.txt"));
    QVERIFY(_writeInPlace(watchedFile, QByteArrayLiteral("before")));

    QGCFileWatcher watcher;
    watcher.setDebounceDelay(0);
    QSignalSpy directorySpy(&watcher, &QGCFileWatcher::directoryChanged);
    QSignalSpy fileSpy(&watcher, &QGCFileWatcher::fileChanged);
    QVERIFY(watcher.watchDirectory(tempDir.path(), nullptr));
    QVERIFY(watcher.watchFile(watchedFile, nullptr));

    QVERIFY(_writeInPlace(watchedFile, QByteArrayLiteral("after")));
    QVERIFY(_writeInPlace(dir.filePath(QStringLiteral("added.txt")), QByteArrayLiteral("new")));
    _watcherNotificationsDelivered = QTest::qWaitFor([&]() { return !directorySpy.isEmpty() && !fileSpy.isEmpty(); },
                                                     QDeadlineTimer(TestTimeout::mediumDuration()));
}

QByteArray VehicleProfileManagerTest::_vehicleJson(const QString& id, const QString& name)
{
    QJsonObject json;
    json["groundStation"] = "QGroundControl";
    json["fileType"] = "Vehicle";
    json["version"] = 1;
    json["id"] = id;
    json["name"] = name;
    json["mavType"] = "MAV_TYPE_QUADROTOR";
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

bool VehicleProfileManagerTest::_writeInPlace(const QString& filePath, const QByteArray& bytes)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const bool written = (file.write(bytes) == bytes.size());
    file.close();
    return written;
}

void VehicleProfileManagerTest::_createNamesFileFromName_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expectedFileName");

    QTest::newRow("plain") << QStringLiteral("Survey Quad") << QStringLiteral("Survey Quad.vehicle");
    QTest::newRow("unsafe characters") << QStringLiteral("a/b\\c:d*e?f\"g<h>i|j\tk")
                                       << QStringLiteral("a_b_c_d_e_f_g_h_i_j_k.vehicle");
    QTest::newRow("edge dots and spaces") << QStringLiteral(" ..hidden. ") << QStringLiteral("hidden.vehicle");
    QTest::newRow("empty") << QString() << QStringLiteral("Vehicle.vehicle");
    QTest::newRow("only dots") << QStringLiteral("...") << QStringLiteral("Vehicle.vehicle");
    QTest::newRow("windows device name") << QStringLiteral("con") << QStringLiteral("con_.vehicle");
    QTest::newRow("windows device name with extension")
        << QStringLiteral("Aux.Quad") << QStringLiteral("Aux_.Quad.vehicle");
    QTest::newRow("decomposed unicode") << QStringLiteral("Cafe\u0301") << QStringLiteral("Caf\u00e9.vehicle");
    QTest::newRow("long") << QString(80, QLatin1Char('x'))
                          << QString(50, QLatin1Char('x')) + QStringLiteral(".vehicle");
}

void VehicleProfileManagerTest::_createNamesFileFromName()
{
    QFETCH(QString, name);
    QFETCH(QString, expectedFileName);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VehicleProfileManager manager(tempDir.path());

    VehicleProfileEntry* const entry = manager.createVehicle(name);
    QVERIFY(entry);
    QCOMPARE(entry->fileName(), expectedFileName);
    QCOMPARE(manager.vehicles()->count(), 1);

    // The file holds the name exactly as typed; only the file name is made safe.
    QFile file(QDir(tempDir.path()).filePath(expectedFileName));
    QVERIFY(file.open(QIODevice::ReadOnly));
    VehicleProfile onDisk;
    QString errorString;
    QVERIFY2(onDisk.loadJson(file.readAll(), errorString), qPrintable(errorString));
    QCOMPARE(onDisk.name(), name);
    QCOMPARE(onDisk.id(), entry->id());
}

void VehicleProfileManagerTest::_createAddsSuffixOnNameClash()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VehicleProfileManager manager(tempDir.path());

    VehicleProfileEntry* const first = manager.createVehicle(QStringLiteral("Quad"));
    VehicleProfileEntry* const second = manager.createVehicle(QStringLiteral("Quad"));
    VehicleProfileEntry* const third = manager.createVehicle(QStringLiteral("Quad"));
    // Compared without case, so this does not land on a name macOS or Windows would merge.
    VehicleProfileEntry* const lowerCase = manager.createVehicle(QStringLiteral("quad"));
    QVERIFY(first && second && third && lowerCase);

    QCOMPARE(first->fileName(), QStringLiteral("Quad.vehicle"));
    QCOMPARE(second->fileName(), QStringLiteral("Quad (2).vehicle"));
    QCOMPARE(third->fileName(), QStringLiteral("Quad (3).vehicle"));
    QCOMPARE(lowerCase->fileName(), QStringLiteral("quad (4).vehicle"));

    QCOMPARE(QDir(tempDir.path()).entryList({QStringLiteral("*.vehicle")}, QDir::Files).size(), 4);
    QCOMPARE(manager.vehicles()->count(), 4);
    QVERIFY(first->id() != second->id());
    QVERIFY(second->id() != third->id());
}

void VehicleProfileManagerTest::_createKeepsOtherUnicodeFormApart()
{
    // APFS finds "Café" whether the é is one code point or "e" plus a combining accent, so a
    // name that differs only in that form must still get its own file.
    const QString composed = QStringLiteral("Caf\u00e9");
    const QString decomposed = QStringLiteral("Cafe\u0301");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir dir(tempDir.path());
    QVERIFY(_writeInPlace(dir.filePath(decomposed + QStringLiteral(".vehicle")), _vehicleJson(kIdA, decomposed)));

    VehicleProfileManager manager(tempDir.path());
    QCOMPARE(manager.vehicles()->count(), 1);

    VehicleProfileEntry* const fromComposed = manager.createVehicle(composed);
    VehicleProfileEntry* const fromDecomposed = manager.createVehicle(decomposed);
    QVERIFY(fromComposed && fromDecomposed);
    QCOMPARE(fromComposed->fileName(), composed + QStringLiteral(" (2).vehicle"));
    QCOMPARE(fromDecomposed->fileName(), composed + QStringLiteral(" (3).vehicle"));
    QCOMPARE(dir.entryList({QStringLiteral("*.vehicle")}, QDir::Files).size(), 3);

    VehicleProfileManager reloaded(tempDir.path());
    QCOMPARE(reloaded.vehicles()->count(), 3);
    QVERIFY(reloaded.vehicleById(kIdA));
    QVERIFY(reloaded.vehicleById(fromComposed->id()));
    QVERIFY(reloaded.vehicleById(fromDecomposed->id()));
}

void VehicleProfileManagerTest::_editSavesToOwnFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString id;
    {
        VehicleProfileManager manager(tempDir.path());
        VehicleProfileEntry* const entry = manager.createVehicle(QStringLiteral("Mapper"));
        QVERIFY(entry);
        id = entry->id();

        entry->setMavType(MAV_TYPE_FIXED_WING);
        entry->setWeightKg(3.5);
        entry->setMaxPayloadKg(0.8);
        entry->setMaxFlightTimeMinutes(45);
        entry->setBatteries({QVariantMap{{"cellCount", 6}, {"capacityMah", 10000}}});
        entry->setSensors({QVariantMap{{"type", "camera"}, {"model", "RX1"}}});
        entry->setFlightControllerHardware(QStringLiteral("Pixhawk 6C"));
        entry->setFlightControllerFirmware(QGCMAVLink::FirmwareClassArduPilot);
        entry->setFlightControllerFirmwareVersion(QStringLiteral("4.5.0"));
        entry->setNotes(QStringLiteral("Spare props in the case"));
        QVERIFY(manager.saveVehicle(entry));
    }

    QCOMPARE(QDir(tempDir.path()).entryList({QStringLiteral("*.vehicle")}, QDir::Files),
             QStringList{QStringLiteral("Mapper.vehicle")});

    // A fresh manager reads back exactly what the edit saved.
    VehicleProfileManager reloaded(tempDir.path());
    QCOMPARE(reloaded.vehicles()->count(), 1);
    const VehicleProfileEntry* const entry = entryAt(reloaded, 0);
    QCOMPARE(entry->id(), id);
    QCOMPARE(entry->mavType(), static_cast<int>(MAV_TYPE_FIXED_WING));
    QCOMPARE(entry->weightKg(), 3.5);
    QCOMPARE(entry->maxPayloadKg(), 0.8);
    QCOMPARE(entry->maxFlightTimeMinutes(), 45);
    QCOMPARE(entry->batteries().size(), 1);
    QCOMPARE(entry->batteries().first().toMap().value("cellCount").toInt(), 6);
    QCOMPARE(entry->batteries().first().toMap().value("capacityMah").toInt(), 10000);
    QCOMPARE(entry->sensors().size(), 1);
    QCOMPARE(entry->sensors().first().toMap().value("model").toString(), QStringLiteral("RX1"));
    QCOMPARE(entry->flightControllerHardware(), QStringLiteral("Pixhawk 6C"));
    QCOMPARE(entry->flightControllerFirmware(), static_cast<int>(QGCMAVLink::FirmwareClassArduPilot));
    QCOMPARE(entry->flightControllerFirmwareVersion(), QStringLiteral("4.5.0"));
    QCOMPARE(entry->notes(), QStringLiteral("Spare props in the case"));
}

void VehicleProfileManagerTest::_renameKeepsFileName()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VehicleProfileManager manager(tempDir.path());

    VehicleProfileEntry* const entry = manager.createVehicle(QStringLiteral("Alpha"));
    QVERIFY(entry);
    const QString id = entry->id();

    entry->setName(QStringLiteral("Bravo"));
    QVERIFY(manager.saveVehicle(entry));

    QCOMPARE(entry->fileName(), QStringLiteral("Alpha.vehicle"));
    QCOMPARE(QDir(tempDir.path()).entryList({QStringLiteral("*.vehicle")}, QDir::Files),
             QStringList{QStringLiteral("Alpha.vehicle")});

    VehicleProfileManager reloaded(tempDir.path());
    QCOMPARE(reloaded.vehicles()->count(), 1);
    QCOMPARE(entryAt(reloaded, 0)->name(), QStringLiteral("Bravo"));
    QCOMPARE(entryAt(reloaded, 0)->id(), id);
    QCOMPARE(entryAt(reloaded, 0)->fileName(), QStringLiteral("Alpha.vehicle"));
}

void VehicleProfileManagerTest::_deleteRemovesFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VehicleProfileManager manager(tempDir.path());

    VehicleProfileEntry* const keep = manager.createVehicle(QStringLiteral("Keep"));
    VehicleProfileEntry* const remove = manager.createVehicle(QStringLiteral("Remove"));
    QVERIFY(keep && remove);
    const QString removedPath = remove->filePath();

    QVERIFY(manager.deleteVehicle(remove));
    QVERIFY(!QFileInfo::exists(removedPath));
    QCOMPARE(manager.vehicles()->count(), 1);
    QCOMPARE(entryAt(manager, 0), keep);

    // The watcher reports the manager's own delete; the list must not change again.
    QSignalSpy countSpy(manager.vehicles(), &QmlObjectListModel::countChanged);
    QVERIFY_NO_SIGNAL_WAIT(countSpy, TestTimeout::shortMs());
    QCOMPARE(manager.vehicles()->count(), 1);
}

void VehicleProfileManagerTest::_ownSaveIsNotAnOutsideChange()
{
    if (!_watcherNotificationsDelivered) {
        QSKIP("File change notifications are not delivered in this environment");
    }
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VehicleProfileManager manager(tempDir.path());

    VehicleProfileEntry* const saved = manager.createVehicle(QStringLiteral("Saved"));
    VehicleProfileEntry* const unsaved = manager.createVehicle(QStringLiteral("Unsaved"));
    QVERIFY(saved && unsaved);

    saved->setNotes(QStringLiteral("saved note"));
    unsaved->setNotes(QStringLiteral("unsaved note"));
    QSignalSpy savedSpy(saved, &VehicleProfileEntry::profileChanged);
    QSignalSpy unsavedSpy(unsaved, &VehicleProfileEntry::profileChanged);
    QVERIFY(manager.saveVehicle(saved));
    // Edited again before the watcher reports the save: the report must not undo this edit.
    saved->setNotes(QStringLiteral("edited after save"));
    QCOMPARE(savedSpy.count(), 1);

    // The watcher reports the save. Neither entry may reload: that would discard the unsaved
    // edits.
    QVERIFY_NO_SIGNAL_WAIT(unsavedSpy, TestTimeout::shortMs());
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(saved->notes(), QStringLiteral("edited after save"));
    QCOMPARE(unsaved->notes(), QStringLiteral("unsaved note"));
    QCOMPARE(manager.vehicles()->count(), 2);

    // Positive control: the watcher is live, and a real outside change does reload.
    QVERIFY(_writeInPlace(unsaved->filePath(), _vehicleJson(unsaved->id(), "Changed outside")));
    QTRY_COMPARE_WITH_TIMEOUT(unsaved->name(), QStringLiteral("Changed outside"), TestTimeout::mediumMs());
    QCOMPARE(saved->notes(), QStringLiteral("edited after save"));
}

void VehicleProfileManagerTest::_brokenFileSkippedWithWarning()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir dir(tempDir.path());
    QVERIFY(_writeInPlace(dir.filePath("Good.vehicle"), _vehicleJson(kIdA, "Good")));
    QVERIFY(_writeInPlace(dir.filePath("Broken.vehicle"), QByteArrayLiteral("{ this is not json")));

    expectLogMessage(kManagerLogCategory, QtWarningMsg, QRegularExpression(QStringLiteral("Skipping.*Broken")));
    VehicleProfileManager manager(tempDir.path());
    verifyExpectedLogMessage();
    QCOMPARE(manager.vehicles()->count(), 1);
    QCOMPARE(entryAt(manager, 0)->id(), QString(kIdA));

    if (!_watcherNotificationsDelivered) {
        QSKIP("File change notifications are not delivered in this environment");
    }

    QVERIFY(_writeInPlace(dir.filePath("Other.vehicle"),
                          _vehicleJson(QStringLiteral("8f0c2d4e-0000-0000-0000-00000000000c"), "Other")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.vehicles()->count(), 2, TestTimeout::mediumMs());

    // A file caught half-written is skipped too, and loads once the write completes. The
    // marker file is written after it, so the rescan that loads the marker has read it.
    const QByteArray fullJson = _vehicleJson(kIdB, "Late");
    const QString latePath = dir.filePath("Late.vehicle");
    expectLogMessage(kManagerLogCategory, QtWarningMsg, QRegularExpression(QStringLiteral("Skipping.*Late")));
    QVERIFY(_writeInPlace(latePath, fullJson.left(fullJson.size() / 2)));
    QVERIFY(_writeInPlace(dir.filePath("Marker.vehicle"),
                          _vehicleJson(QStringLiteral("8f0c2d4e-0000-0000-0000-00000000000d"), "Marker")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.vehicles()->count(), 3, TestTimeout::mediumMs());
    verifyExpectedLogMessage();
    QVERIFY(!manager.vehicleById(kIdB));

    QVERIFY(_writeInPlace(latePath, fullJson));
    QTRY_VERIFY_WITH_TIMEOUT(manager.vehicleById(kIdB), TestTimeout::mediumMs());
    QCOMPARE(manager.vehicles()->count(), 4);
    QVERIFY(manager.vehicleById(kIdA));
}

void VehicleProfileManagerTest::_outsideAddAppears()
{
    if (!_watcherNotificationsDelivered) {
        QSKIP("File change notifications are not delivered in this environment");
    }
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VehicleProfileManager manager(tempDir.path());
    QCOMPARE(manager.vehicles()->count(), 0);

    QVERIFY(_writeInPlace(QDir(tempDir.path()).filePath("Downloaded.vehicle"), _vehicleJson(kIdA, "Downloaded")));

    QTRY_COMPARE_WITH_TIMEOUT(manager.vehicles()->count(), 1, TestTimeout::mediumMs());
    QCOMPARE(entryAt(manager, 0)->id(), QString(kIdA));
    QCOMPARE(entryAt(manager, 0)->name(), QStringLiteral("Downloaded"));
    QCOMPARE(entryAt(manager, 0)->fileName(), QStringLiteral("Downloaded.vehicle"));
}

void VehicleProfileManagerTest::_outsideEditReloadsSameEntry()
{
    if (!_watcherNotificationsDelivered) {
        QSKIP("File change notifications are not delivered in this environment");
    }
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VehicleProfileManager manager(tempDir.path());
    VehicleProfileEntry* const entry = manager.createVehicle(QStringLiteral("Original"));
    QVERIFY(entry);
    const QString id = entry->id();

    // An atomic replace, as QSaveFile or a download tool does it.
    QSaveFile saveFile(entry->filePath());
    QVERIFY(saveFile.open(QIODevice::WriteOnly));
    QVERIFY(saveFile.write(_vehicleJson(id, "Replaced")) > 0);
    QVERIFY(saveFile.commit());
    QTRY_COMPARE_WITH_TIMEOUT(entry->name(), QStringLiteral("Replaced"), TestTimeout::mediumMs());

    // Then an in-place write, which only a watch on the new file sees.
    QVERIFY(_writeInPlace(entry->filePath(), _vehicleJson(id, "Rewritten")));
    QTRY_COMPARE_WITH_TIMEOUT(entry->name(), QStringLiteral("Rewritten"), TestTimeout::mediumMs());

    QCOMPARE(manager.vehicles()->count(), 1);
    QCOMPARE(entryAt(manager, 0), entry);
    QCOMPARE(entry->id(), id);
}

void VehicleProfileManagerTest::_outsideDeleteRemoves()
{
    if (!_watcherNotificationsDelivered) {
        QSKIP("File change notifications are not delivered in this environment");
    }
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    VehicleProfileManager manager(tempDir.path());
    VehicleProfileEntry* const entry = manager.createVehicle(QStringLiteral("Doomed"));
    QVERIFY(entry);

    QVERIFY(QFile::remove(entry->filePath()));

    QTRY_COMPARE_WITH_TIMEOUT(manager.vehicles()->count(), 0, TestTimeout::mediumMs());
}

void VehicleProfileManagerTest::_duplicateIdSkipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir dir(tempDir.path());
    QVERIFY(_writeInPlace(dir.filePath("A Original.vehicle"), _vehicleJson(kIdA, "Original")));
    QVERIFY(_writeInPlace(dir.filePath("B Copy.vehicle"), _vehicleJson(kIdA, "Copy")));

    expectLogMessage(kManagerLogCategory, QtWarningMsg,
                     QRegularExpression(QStringLiteral("Skipping.*B Copy.*another file already has vehicle id")));
    VehicleProfileManager manager(tempDir.path());
    verifyExpectedLogMessage();
    QCOMPARE(manager.vehicles()->count(), 1);
    QCOMPARE(entryAt(manager, 0)->fileName(), QStringLiteral("A Original.vehicle"));

    if (!_watcherNotificationsDelivered) {
        QSKIP("File change notifications are not delivered in this environment");
    }

    // Once the first file is gone, the id is free and the copy loads, paired with its own file.
    QVERIFY(QFile::remove(dir.filePath("A Original.vehicle")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.vehicleById(kIdA) ? manager.vehicleById(kIdA)->fileName() : QString(),
                              QStringLiteral("B Copy.vehicle"), TestTimeout::mediumMs());
    QCOMPARE(manager.vehicles()->count(), 1);
}

void VehicleProfileManagerTest::_idChangedOnDiskReplacesEntry()
{
    if (!_watcherNotificationsDelivered) {
        QSKIP("File change notifications are not delivered in this environment");
    }
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString filePath = QDir(tempDir.path()).filePath("Shared.vehicle");
    QVERIFY(_writeInPlace(filePath, _vehicleJson(kIdA, "First")));

    VehicleProfileManager manager(tempDir.path());
    QCOMPARE(manager.vehicles()->count(), 1);
    const QPointer<VehicleProfileEntry> original = entryAt(manager, 0);

    QVERIFY(_writeInPlace(filePath, _vehicleJson(kIdB, "Second")));

    // An entry's id never changes: a different vehicle in the same file is a new entry.
    QTRY_VERIFY_WITH_TIMEOUT(manager.vehicleById(kIdB), TestTimeout::mediumMs());
    QCOMPARE(manager.vehicles()->count(), 1);
    QVERIFY(!manager.vehicleById(kIdA));
    QVERIFY(entryAt(manager, 0) != original.data());
    QCOMPARE(entryAt(manager, 0)->filePath(), filePath);
}

void VehicleProfileManagerTest::_followsSavePathChange()
{
    AppSettings* const appSettings = SettingsManager::instance()->appSettings();
    QVERIFY(appSettings);
    VehicleProfileManager* const manager = VehicleProfileManager::instance();
    QVERIFY(manager);
    QCOMPARE(manager->folder(), QDir::cleanPath(appSettings->vehicleSavePath()));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(QDir(tempDir.path()).mkpath(QStringLiteral("Vehicles")));
    QVERIFY(
        _writeInPlace(QDir(tempDir.path()).filePath("Vehicles/Elsewhere.vehicle"), _vehicleJson(kIdA, "Elsewhere")));

    Fact* const savePathFact = appSettings->savePath();
    QVERIFY(savePathFact);
    const QVariant originalSavePath = savePathFact->rawValue();
    const auto guard = qScopeGuard([savePathFact, originalSavePath] { savePathFact->setRawValue(originalSavePath); });

    savePathFact->setRawValue(tempDir.path());

    const QString expectedFolder = QDir::cleanPath(appSettings->vehicleSavePath());
    QTRY_COMPARE_WITH_TIMEOUT(manager->folder(), expectedFolder, TestTimeout::mediumMs());
    QCOMPARE(manager->vehicles()->count(), 1);
    QCOMPARE(entryAt(*manager, 0)->id(), QString(kIdA));
}
