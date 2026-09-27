#include "AppSettingsTest.h"

#include <QtCore/QDir>
#include <QtCore/QScopeGuard>
#include <QtCore/QTemporaryDir>

#include "AppSettings.h"
#include "FirmwarePluginManager.h"
#include "QGCMAVLink.h"
#include "SettingsManager.h"

UT_REGISTER_TEST(AppSettingsTest, TestLabel::Unit)

void AppSettingsTest::_preferredFirmwareClassEnumFiltered()
{
    _verifyFirmwareClassEnumFiltered(SettingsManager::instance()->appSettings()->preferredFirmwareClass());
}

void AppSettingsTest::_offlineEditingFirmwareClassEnumFiltered()
{
    _verifyFirmwareClassEnumFiltered(SettingsManager::instance()->appSettings()->offlineEditingFirmwareClass());
}

void AppSettingsTest::_vehicleSaveFolderExistsOnStart()
{
    // The running instance already applied its boot-time save path before this test function
    // runs, so the Vehicles folder must already exist under it - this is what "on start" means.
    AppSettings* const appSettings = SettingsManager::instance()->appSettings();
    QVERIFY(appSettings);

    const QString vehicleSavePath = appSettings->vehicleSavePath();
    QVERIFY(!vehicleSavePath.isEmpty());
    QVERIFY2(QDir(vehicleSavePath).exists(), qPrintable(vehicleSavePath));
}

void AppSettingsTest::_vehicleSaveFolderCreatedAfterSavePathChange()
{
    AppSettings* const appSettings = SettingsManager::instance()->appSettings();
    QVERIFY(appSettings);

    // QTemporaryDir both creates the directory and removes it (and everything under it,
    // including the Vehicles folder this test verifies) when it goes out of scope.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Fact* const savePathFact = appSettings->savePath();
    QVERIFY(savePathFact);
    const QVariant originalSavePath = savePathFact->rawValue();
    const auto guard = qScopeGuard([savePathFact, originalSavePath] { savePathFact->setRawValue(originalSavePath); });

    savePathFact->setRawValue(tempDir.path());

    const QString vehicleSavePath = appSettings->vehicleSavePath();
    QVERIFY(!vehicleSavePath.isEmpty());
    QVERIFY2(QDir(vehicleSavePath).exists(), qPrintable(vehicleSavePath));
}

void AppSettingsTest::_verifyFirmwareClassEnumFiltered(Fact *fact)
{
    const QList<QGCMAVLink::FirmwareClass_t> supportedClasses = FirmwarePluginManager::instance()->supportedFirmwareClasses();

    const QVariantList enumValues = fact->enumValues();
    QCOMPARE(fact->enumStrings().count(), enumValues.count());
    QVERIFY(!enumValues.isEmpty());

    for (const QVariant &enumValue : enumValues) {
        const auto firmwareClass = static_cast<QGCMAVLink::FirmwareClass_t>(enumValue.toUInt());
        QVERIFY2(supportedClasses.contains(firmwareClass),
                 qPrintable(QStringLiteral("%1 enum offers unsupported firmware class %2").arg(fact->name()).arg(enumValue.toUInt())));
    }

    QVERIFY2(supportedClasses.contains(static_cast<QGCMAVLink::FirmwareClass_t>(fact->rawValue().toUInt())),
             qPrintable(QStringLiteral("%1 value is an unsupported firmware class %2").arg(fact->name()).arg(fact->rawValue().toUInt())));
}
