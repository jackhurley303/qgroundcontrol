#include "PluginInstallerTest.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QStandardPaths>

#include "PluginInstaller.h"
#include "PluginManifest.h"

#include "miniz.h"

void PluginInstallerTest::init()
{
    TempDirectoryTest::init();

    // Redirects QStandardPaths::AppDataLocation (and therefore
    // PluginInstaller::userPluginsDir()) to a Qt-managed, test-scoped location instead
    // of the developer's real application-support directory.
    QStandardPaths::setTestModeEnabled(true);
    QDir(PluginInstaller::userPluginsDir()).removeRecursively();
}

QJsonObject PluginInstallerTest::_validManifestJson(const QString& id, const QString& version)
{
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("name")] = QStringLiteral("Test Package");
    json[QStringLiteral("version")] = version;
    json[QStringLiteral("vendor")] = QStringLiteral("Test Org");
    json[QStringLiteral("description")] = QStringLiteral("Installer fixture");
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    hostVersion[QStringLiteral("max")] = QString();
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("contributes")] = QJsonObject();
    return json;
}

QString PluginInstallerTest::_writeZip(const QString& zipRelPath, const QMap<QString, QByteArray>& entries)
{
    const QString zipPath = tempPath(zipRelPath);

    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_file(&zip, zipPath.toUtf8().constData(), 0)) {
        return QString();
    }

    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QByteArray archiveName = it.key().toUtf8();
        const QByteArray& content = it.value();
        if (!mz_zip_writer_add_mem(&zip, archiveName.constData(), content.constData(), static_cast<size_t>(content.size()), MZ_DEFAULT_COMPRESSION)) {
            mz_zip_writer_end(&zip);
            return QString();
        }
    }

    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        return QString();
    }
    mz_zip_writer_end(&zip);

    return zipPath;
}

void PluginInstallerTest::_installGoodZipSucceeds_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.goodpkg"))).toJson();
    entries[QStringLiteral("qml/View.qml")] = QByteArrayLiteral("import QtQuick\nItem {}\n");

    const QString zipPath = _writeZip(QStringLiteral("good.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    QVERIFY2(result.success, qPrintable(result.errorString));
    QCOMPARE(result.pluginId, QStringLiteral("org.test.goodpkg"));

    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath("org.test.goodpkg");
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/qgcplugin.json")));
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/qml/View.qml")));
}

void PluginInstallerTest::_installManifestlessZipRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qml/View.qml")] = QByteArrayLiteral("import QtQuick\nItem {}\n");

    const QString zipPath = _writeZip(QStringLiteral("nomanifest.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("qgcplugin.json")), qPrintable(result.errorString));

    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.goodpkg")).exists());
}

void PluginInstallerTest::_installMalformedManifestRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QByteArrayLiteral("{ this is not valid json");

    const QString zipPath = _writeZip(QStringLiteral("malformed.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("malformed")), qPrintable(result.errorString));
}

void PluginInstallerTest::_installMissingFileRejected_test()
{
    const PluginInstallResult result = PluginInstaller::installFromFile(tempPath(QStringLiteral("nonexistent.qgcplugin")));
    QVERIFY(!result.success);
    QVERIFY(!result.errorString.isEmpty());
}

void PluginInstallerTest::_installZipSlipEntryRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.slippkg"))).toJson();
    entries[QStringLiteral("../../escaped.txt")] = QByteArrayLiteral("payload");

    const QString zipPath = _writeZip(QStringLiteral("slip.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("unsafe")), qPrintable(result.errorString));

    // The escape attempt must not have landed anywhere outside the plugins dir either.
    QVERIFY(!QFile::exists(tempPath(QStringLiteral("../escaped.txt"))));
}

void PluginInstallerTest::_idCollisionReplaces_test()
{
    QMap<QString, QByteArray> v1Entries;
    v1Entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.collide"), QStringLiteral("1.0.0"))).toJson();
    v1Entries[QStringLiteral("old-only.txt")] = QByteArrayLiteral("v1");
    const QString v1Zip = _writeZip(QStringLiteral("v1.qgcplugin"), v1Entries);
    QVERIFY(!v1Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);

    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath("org.test.collide");
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/old-only.txt")));

    QMap<QString, QByteArray> v2Entries;
    v2Entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.collide"), QStringLiteral("2.0.0"))).toJson();
    const QString v2Zip = _writeZip(QStringLiteral("v2.qgcplugin"), v2Entries);
    QVERIFY(!v2Zip.isEmpty());

    const PluginInstallResult result = PluginInstaller::installFromFile(v2Zip);
    QVERIFY2(result.success, qPrintable(result.errorString));

    // v1's file is gone (full replace, not a merge) and the manifest is v2's.
    QVERIFY(!QFile::exists(installedDir + QStringLiteral("/old-only.txt")));
    QFile manifestFile(installedDir + QStringLiteral("/qgcplugin.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject onDisk = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QCOMPARE(onDisk[QStringLiteral("version")].toString(), QStringLiteral("2.0.0"));
}

void PluginInstallerTest::_removeCleansDirectory_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.removeme"))).toJson();
    const QString zipPath = _writeZip(QStringLiteral("removeme.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(zipPath).success);

    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath("org.test.removeme");
    QVERIFY(QDir(installedDir).exists());

    const PluginInstallResult result = PluginInstaller::removePlugin(QStringLiteral("org.test.removeme"));
    QVERIFY2(result.success, qPrintable(result.errorString));
    QVERIFY(!QDir(installedDir).exists());
}

void PluginInstallerTest::_removeUnknownIdFails_test()
{
    const PluginInstallResult result = PluginInstaller::removePlugin(QStringLiteral("org.test.neverinstalled"));
    QVERIFY(!result.success);
    QVERIFY(!result.errorString.isEmpty());
}

UT_REGISTER_TEST(PluginInstallerTest, TestLabel::Unit)
