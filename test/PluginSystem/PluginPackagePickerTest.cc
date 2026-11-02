#include "PluginPackagePickerTest.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>

#include "PluginInstaller.h"
#include "PluginInstallerTest.h"
#include "PluginPackagePicker.h"
#include "PluginTestSupport.h"
#include "QGCPluginManager.h"

namespace {

constexpr const char* kPickedPluginId = "org.test.picked";

QByteArray pickedPackageBytes()
{
    QJsonObject json;
    json[QStringLiteral("id")] = QString::fromLatin1(kPickedPluginId);
    json[QStringLiteral("name")] = QStringLiteral("Picked Package");
    json[QStringLiteral("version")] = QStringLiteral("1.0.0");
    json[QStringLiteral("vendor")] = QStringLiteral("Test Org");
    json[QStringLiteral("description")] = QStringLiteral("Picker fixture");
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    hostVersion[QStringLiteral("max")] = QString();
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("contributes")] = QJsonObject();
    return storedZipArchive({{QStringLiteral("qgcplugin.json"), QJsonDocument(json).toJson()}});
}

bool pickedPluginInstalled()
{
    return QDir(QDir(PluginInstaller::userPluginsDir()).filePath(QString::fromLatin1(kPickedPluginId))).exists();
}

}  // namespace

void PluginPackagePickerTest::init()
{
    TempDirectoryTest::init();

    // Redirects CacheLocation (stagingDir()) and AppDataLocation (userPluginsDir()) to
    // test-scoped locations.
    QStandardPaths::setTestModeEnabled(true);
    QDir(PluginInstaller::userPluginsDir()).removeRecursively();
    QDir(PluginPackagePicker::stagingDir()).removeRecursively();

    QSettings settings;
    settings.remove(QStringLiteral("Plugins/ApprovedDigests"));
    settings.remove(QStringLiteral("Plugins/StagedDigests"));
    settings.remove(QString::fromLatin1(PluginTestSupport::kLoadingPluginIdKey));
    settings.remove(QString::fromLatin1(PluginTestSupport::kCrashedPluginIdKey));
}

QString PluginPackagePickerTest::_writeStagedCopy(const QString& fileName, const QByteArray& bytes)
{
    const QString dir = PluginPackagePicker::stagingDir();
    if (!QDir().mkpath(dir)) {
        return QString();
    }
    const QString path = QDir(dir).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) {
        return QString();
    }
    return path;
}

void PluginPackagePickerTest::_goodPackageInstallsAndCopyDeleted_test()
{
    const QString copy = _writeStagedCopy(QStringLiteral("picked.qgcplugin"), pickedPackageBytes());
    QVERIFY(!copy.isEmpty());

    QGCPluginManager manager;
    PluginPackagePicker picker;
    picker._pluginManager = &manager;

    QCOMPARE(picker.installPickedFile(copy), QString());
    QVERIFY(pickedPluginInstalled());
    QVERIFY(!QFile::exists(copy));
    QVERIFY(!QDir(PluginPackagePicker::stagingDir()).exists());
}

void PluginPackagePickerTest::_badPackageFailsAndCopyDeleted_test()
{
    const QString copy = _writeStagedCopy(QStringLiteral("bad.qgcplugin"), QByteArrayLiteral("not a zip archive"));
    QVERIFY(!copy.isEmpty());

    QGCPluginManager manager;
    PluginPackagePicker picker;
    picker._pluginManager = &manager;

    expectLogMessage("Utilities.QGClibarchive", QtWarningMsg, QRegularExpression("Unrecognized archive format"));
    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("as a zip archive"));
    expectLogMessage("PluginSystem.QGCPluginManager", QtWarningMsg, QRegularExpression("Install failed"));
    QVERIFY(!picker.installPickedFile(copy).isEmpty());
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    QVERIFY(manager.knownPlugins().isEmpty());
    QVERIFY(!QFile::exists(copy));
    QVERIFY(!QDir(PluginPackagePicker::stagingDir()).exists());
}

void PluginPackagePickerTest::_failedCopyInstallsNothingAndCopyDeleted_test()
{
    // A copy that fails part-way leaves a partial file, and the picker reports an empty path.
    const QByteArray whole = pickedPackageBytes();
    const QString partial = _writeStagedCopy(QStringLiteral("picked.qgcplugin"), whole.left(whole.size() / 2));
    QVERIFY(!partial.isEmpty());

    QGCPluginManager manager;
    PluginPackagePicker picker;
    picker._pluginManager = &manager;

    QCOMPARE(picker.installPickedFile(QString()), QString());
    QVERIFY(!pickedPluginInstalled());
    QVERIFY(manager.knownPlugins().isEmpty());
    QVERIFY(!QFile::exists(partial));
    QVERIFY(!QDir(PluginPackagePicker::stagingDir()).exists());
}

void PluginPackagePickerTest::_emptyPathInstallsNothing_test()
{
    QGCPluginManager manager;
    PluginPackagePicker picker;
    picker._pluginManager = &manager;

    QCOMPARE(picker.installPickedFile(QString()), QString());
    QVERIFY(!pickedPluginInstalled());
    QVERIFY(manager.knownPlugins().isEmpty());
    QVERIFY(!QDir(PluginInstaller::userPluginsDir()).exists() || QDir(PluginInstaller::userPluginsDir()).isEmpty());
}

UT_REGISTER_TEST(PluginPackagePickerTest, TestLabel::Unit)
