#include "PluginCatalogManagerTest.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkCacheMetaData>
#include <QtNetwork/QNetworkDiskCache>
#include <QtNetwork/QNetworkRequest>
#include <QtTest/QSignalSpy>

#include "Fact.h"
#include "PluginCatalogManager.h"
#include "PluginInstaller.h"
#include "PluginInstallerTest.h"
#include "PluginSettings.h"
#include "QGCCachedFileDownload.h"
#include "QGCPluginInterface.h"
#include "QGCPluginLoader.h"
#include "QGCPluginManager.h"
#include "SettingsManager.h"

namespace {

constexpr const char* kCatalogLog = "PluginSystem.PluginCatalogManager";
constexpr const char* kDownloadLog = "Utilities.QGCFileDownload";

Fact* catalogUrlFact()
{
    return SettingsManager::instance()->pluginSettings()->catalogUrl();
}

QVariantMap knownPlugin(const QGCPluginManager& pluginManager, const QString& id)
{
    for (const QVariant& value : pluginManager.knownPlugins()) {
        const QVariantMap info = value.toMap();
        if (info.value(QStringLiteral("id")).toString() == id) {
            return info;
        }
    }
    return QVariantMap();
}

}  // namespace

void PluginCatalogManagerTest::init()
{
    TempDirectoryTest::init();

    // Redirects AppDataLocation (and so PluginInstaller::userPluginsDir()) and
    // CacheLocation (the index cache) to test-scoped locations.
    QStandardPaths::setTestModeEnabled(true);
    QDir(PluginInstaller::userPluginsDir()).removeRecursively();
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();

    // Fixture packages are byte-identical every run; a stale consent digest from an earlier
    // run would change what installPlugin() reports.
    QSettings settings;
    settings.remove(QStringLiteral("Plugins/ApprovedDigests"));
    settings.remove(QStringLiteral("Plugins/StagedDigests"));
}

void PluginCatalogManagerTest::cleanup()
{
    catalogUrlFact()->setRawValue(catalogUrlFact()->rawDefaultValue());
    TempDirectoryTest::cleanup();
}

QByteArray PluginCatalogManagerTest::_packageBytes(const QString& id, const QString& version, const QString& tier)
{
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("name")] = QStringLiteral("Catalog Fixture");
    json[QStringLiteral("version")] = version;
    json[QStringLiteral("vendor")] = QStringLiteral("Test Org");
    json[QStringLiteral("description")] = QStringLiteral("Catalog fixture");
    json[QStringLiteral("tier")] = tier;
    if (tier != QStringLiteral("qml")) {
        json[QStringLiteral("apiVersion")] = QGCPluginApiVersion;
    }
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("contributes")] = QJsonObject();
    return storedZipArchive({{QStringLiteral("qgcplugin.json"), QJsonDocument(json).toJson()}});
}

QString PluginCatalogManagerTest::_sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject PluginCatalogManagerTest::_versionJson(const QString& version, const QString& packageKey,
                                                   const QString& url, const QByteArray& packageBytes,
                                                   const QString& tier)
{
    QJsonObject package;
    package[QStringLiteral("url")] = url;
    package[QStringLiteral("sha256")] = _sha256Hex(packageBytes);
    package[QStringLiteral("size")] = static_cast<double>(packageBytes.size());
    QJsonObject packages;
    packages[packageKey] = package;

    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");

    QJsonObject json;
    json[QStringLiteral("version")] = version;
    json[QStringLiteral("tier")] = tier;
    if (tier != QStringLiteral("qml")) {
        json[QStringLiteral("apiVersion")] = QGCPluginApiVersion;
    }
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("released")] = QStringLiteral("2026-10-01");
    json[QStringLiteral("notes")] = QStringLiteral("Fixture notes");
    json[QStringLiteral("packages")] = packages;
    return json;
}

QJsonObject PluginCatalogManagerTest::_pluginJson(const QString& id, const QJsonArray& versions)
{
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("name")] = QStringLiteral("Fixture %1").arg(id);
    json[QStringLiteral("author")] = QStringLiteral("Test Org");
    json[QStringLiteral("summary")] = QStringLiteral("A catalog fixture");
    json[QStringLiteral("repository")] = QStringLiteral("https://example.com/fixture");
    json[QStringLiteral("versions")] = versions;
    return json;
}

QStringList PluginCatalogManagerTest::_userPluginsEntries()
{
    return QDir(PluginInstaller::userPluginsDir())
        .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

QString PluginCatalogManagerTest::_writeFile(const QString& name, const QByteArray& bytes)
{
    QFile file(tempPath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) {
        return QString();
    }
    return file.fileName();
}

QString PluginCatalogManagerTest::_setCatalog(const QJsonArray& plugins, const QString& name, int schemaVersion)
{
    QJsonObject index;
    index[QStringLiteral("schemaVersion")] = schemaVersion;
    index[QStringLiteral("generated")] = QStringLiteral("2026-10-01");
    index[QStringLiteral("plugins")] = plugins;
    const QString path = _writeFile(name, QJsonDocument(index).toJson());
    catalogUrlFact()->setRawValue(path);
    return path;
}

bool PluginCatalogManagerTest::_fetch(PluginCatalogManager& catalog)
{
    catalog.fetch();
    return QTest::qWaitFor([&catalog]() { return catalog.fetchState() != PluginCatalogManager::FetchState::Fetching; },
                           TestTimeout::mediumMs());
}

QString PluginCatalogManagerTest::_startInstall(PluginCatalogManager& catalog, const QString& id)
{
    if (!catalog.install(id).isEmpty() || !catalog._installDir) {
        return QString();
    }
    return catalog._installDir->path();
}

void PluginCatalogManagerTest::_verifyRefused(const QSignalSpy& spy, const QString& id, const QString& errorPattern,
                                              const QString& stagingDir, const QStringList& pluginsBefore)
{
    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.first();
    QCOMPARE(args.at(0).toString(), id);
    QCOMPARE(args.at(1).toBool(), false);
    const QString error = args.at(2).toString();
    QVERIFY2(QRegularExpression(errorPattern).match(error).hasMatch(), qPrintable(error));
    QCOMPARE(args.at(3).toBool(), false);

    QVERIFY(!stagingDir.isEmpty());
    QVERIFY2(!QFileInfo::exists(stagingDir), qPrintable(stagingDir));
    QVERIFY(!QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(id)));
    QCOMPARE(_userPluginsEntries(), pluginsBefore);
}

void PluginCatalogManagerTest::_emptyUrlTurnsCatalogOff_test()
{
    const QByteArray zip = _packageBytes(QStringLiteral("org.test.catalog.off"), QStringLiteral("1.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("off.qgcplugin"), zip);
    (void) _setCatalog({_pluginJson(QStringLiteral("org.test.catalog.off"),
                                    {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip)})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);

    // A fresh manager with no URL reads nothing and reports nothing
    catalogUrlFact()->setRawValue(QString());
    QSignalSpy stateSpy(&catalog, &PluginCatalogManager::fetchStateChanged);
    QSignalSpy entriesSpy(&catalog, &PluginCatalogManager::entriesChanged);
    catalog.fetch();
    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Idle);
    QCOMPARE(stateSpy.count(), 0);
    QCOMPARE(entriesSpy.count(), 0);
    QVERIFY(!catalog._indexDownload);

    // Emptying the URL after a fetch turns the catalog off: its entries go
    catalogUrlFact()->setRawValue(tempPath(QStringLiteral("index.json")));
    QVERIFY(_fetch(catalog));
    QCOMPARE(catalog.entries().size(), 1);
    catalogUrlFact()->setRawValue(QStringLiteral("   "));
    catalog.fetch();
    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Idle);
    QVERIFY(catalog.entries().isEmpty());
    QVERIFY(catalog.fetchError().isEmpty());
}

void PluginCatalogManagerTest::_fetchListsEntries_test()
{
    const QString id = QStringLiteral("org.test.catalog.list");
    const QByteArray zip = _packageBytes(id, QStringLiteral("2.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("list.qgcplugin"), zip);
    QJsonObject incompatible = _versionJson(QStringLiteral("3.0.0"), QStringLiteral("any"), zipPath, zip);
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("999.0");
    incompatible[QStringLiteral("hostVersion")] = hostVersion;
    (void) _setCatalog(
        {_pluginJson(id, {_versionJson(QStringLiteral("2.0.0"), QStringLiteral("any"), zipPath, zip), incompatible})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Ready);
    QVERIFY(catalog.fetchError().isEmpty());
    QCOMPARE(catalog.entries().size(), 1);

    // The offered version is the newest that runs here, not the newest listed
    const QVariantMap entry = catalog.entries().first().toMap();
    QCOMPARE(entry.value(QStringLiteral("id")).toString(), id);
    QCOMPARE(entry.value(QStringLiteral("author")).toString(), QStringLiteral("Test Org"));
    QCOMPARE(entry.value(QStringLiteral("compatible")).toBool(), true);
    QCOMPARE(entry.value(QStringLiteral("version")).toString(), QStringLiteral("2.0.0"));
    QCOMPARE(entry.value(QStringLiteral("tier")).toString(), QStringLiteral("qml"));
    QCOMPARE(entry.value(QStringLiteral("size")).toLongLong(), static_cast<qint64>(zip.size()));
    QCOMPARE(entry.value(QStringLiteral("installed")).toBool(), false);
    QCOMPARE(entry.value(QStringLiteral("updateAvailable")).toBool(), false);
    QVERIFY(catalog.availableUpdates().isEmpty());
}

void PluginCatalogManagerTest::_unreachableUrlChangesNothing_test_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("downloadWarning");

    QTest::newRow("missing local file") << QStringLiteral("missing/index.json") << QStringLiteral("Download error");
    // Port 1 on loopback refuses at once; nothing leaves the machine.
    QTest::newRow("refused https") << QStringLiteral("https://127.0.0.1:1/index.json")
                                   << QStringLiteral("Download error");
}

void PluginCatalogManagerTest::_unreachableUrlChangesNothing_test()
{
    QFETCH(QString, url);
    QFETCH(QString, downloadWarning);
    if (url.startsWith(QStringLiteral("missing/"))) {
        url = tempPath(url);
    }

    const QString id = QStringLiteral("org.test.catalog.keep");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("keep.qgcplugin"), zip);
    (void) _setCatalog({_pluginJson(id, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip)})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    const QVariantList entriesBefore = catalog.entries();
    QCOMPARE(entriesBefore.size(), 1);
    const QStringList pluginsBefore = _userPluginsEntries();

    catalogUrlFact()->setRawValue(url);
    expectLogMessage(kDownloadLog, QtWarningMsg, QRegularExpression(downloadWarning));
    expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Could not read the catalog")));
    QVERIFY(_fetch(catalog));
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();

    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Failed);
    QVERIFY(!catalog.fetchError().isEmpty());
    QCOMPARE(catalog.entries(), entriesBefore);
    QCOMPARE(_userPluginsEntries(), pluginsBefore);
}

void PluginCatalogManagerTest::_newerSchemaListsNothing_test()
{
    const QString id = QStringLiteral("org.test.catalog.schema");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("schema.qgcplugin"), zip);
    const QJsonArray plugins = {
        _pluginJson(id, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip)})};
    (void) _setCatalog(plugins);

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    QCOMPARE(catalog.entries().size(), 1);

    (void) _setCatalog(plugins, QStringLiteral("index-v2.json"), 2);
    expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Refusing the catalog")));
    QVERIFY(_fetch(catalog));
    verifyExpectedLogMessage();

    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Failed);
    QVERIFY(catalog.fetchError().contains(QStringLiteral("newer QGroundControl")));
    QVERIFY(catalog.entries().isEmpty());
}

void PluginCatalogManagerTest::_urlChangedDuringFetchFetchesAgain_test()
{
    const QString firstId = QStringLiteral("org.test.catalog.first");
    const QString secondId = QStringLiteral("org.test.catalog.second");
    const QByteArray zip = _packageBytes(firstId, QStringLiteral("1.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("first.qgcplugin"), zip);
    (void) _setCatalog(
        {_pluginJson(secondId, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip)})},
        QStringLiteral("second.json"));
    const QString secondUrl = catalogUrlFact()->rawValue().toString();
    (void) _setCatalog(
        {_pluginJson(firstId, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip)})},
        QStringLiteral("first.json"));

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    catalog.fetch();
    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Fetching);

    // The second request is not started on top of the first; the first one's result is
    // dropped and the new URL fetched instead
    catalogUrlFact()->setRawValue(secondUrl);
    QVERIFY(_fetch(catalog));
    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Ready);
    QCOMPARE(catalog.entries().size(), 1);
    QCOMPARE(catalog.entries().first().toMap().value(QStringLiteral("id")).toString(), secondId);
}

void PluginCatalogManagerTest::_sourcePolicy_test_data()
{
    QTest::addColumn<QString>("location");
    QTest::addColumn<bool>("allowLocal");
    QTest::addColumn<bool>("allowed");

    const QString localPath = QDir::temp().filePath(QStringLiteral("index.json"));
    const QString fileUrl = QUrl::fromLocalFile(localPath).toString();

    QTest::newRow("https") << QStringLiteral("https://example.com/index.json") << false << true;
    QTest::newRow("http") << QStringLiteral("http://example.com/index.json") << true << false;
    QTest::newRow("uppercase scheme") << QStringLiteral("HTTPS://example.com/index.json") << true << false;
    QTest::newRow("https without host") << QStringLiteral("https:///index.json") << true << false;
    QTest::newRow("ftp") << QStringLiteral("ftp://example.com/index.json") << true << false;
    QTest::newRow("local path, local allowed") << localPath << true << true;
    QTest::newRow("local path, remote index") << localPath << false << false;
    QTest::newRow("file url, local allowed") << fileUrl << true << true;
    QTest::newRow("file url, remote index") << fileUrl << false << false;
    QTest::newRow("relative path") << QStringLiteral("index.json") << true << false;
    QTest::newRow("resource") << QStringLiteral(":/index.json") << true << false;
}

void PluginCatalogManagerTest::_sourcePolicy_test()
{
    QFETCH(QString, location);
    QFETCH(bool, allowLocal);
    QFETCH(bool, allowed);

    QCOMPARE(PluginCatalogManager::_isAllowedSource(location, allowLocal), allowed);
}

void PluginCatalogManagerTest::_goodPackageInstalls_test()
{
    const QString id = QStringLiteral("org.test.catalog.good");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.2.0"));
    const QString zipUrl = QUrl::fromLocalFile(_writeFile(QStringLiteral("good.qgcplugin"), zip)).toString();
    // The package sits under this build's own platform key, which is what install() asks for
    const QString indexPath = _setCatalog(
        {_pluginJson(id, {_versionJson(QStringLiteral("1.2.0"), QGCPluginLoader::platformKey(), zipUrl, zip)})});
    catalogUrlFact()->setRawValue(QUrl::fromLocalFile(indexPath).toString());

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Ready);

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    const QString stagingDir = _startInstall(catalog, id);
    QVERIFY(!stagingDir.isEmpty());
    QCOMPARE(catalog.installingId(), id);
    const QString pluginsDir = PluginInstaller::userPluginsDir();
    QVERIFY2(!QDir::cleanPath(stagingDir).startsWith(QDir::cleanPath(pluginsDir)), qPrintable(stagingDir));
    QVERIFY_SIGNAL_WAIT(spy, TestTimeout::mediumMs());

    const QList<QVariant> args = spy.first();
    QCOMPARE(args.at(0).toString(), id);
    QCOMPARE(args.at(1).toBool(), true);
    QCOMPARE(args.at(2).toString(), QString());
    QCOMPARE(args.at(3).toBool(), false);
    QVERIFY(catalog.installingId().isEmpty());
    QVERIFY(!QFileInfo::exists(stagingDir));

    QVERIFY(QFileInfo::exists(QDir(pluginsDir).filePath(QStringLiteral("%1/qgcplugin.json").arg(id))));
    QCOMPARE(knownPlugin(pluginManager, id).value(QStringLiteral("state")).toString(), QStringLiteral("Active"));
    const QVariantMap entry = catalog.entries().first().toMap();
    QCOMPARE(entry.value(QStringLiteral("installed")).toBool(), true);
    QCOMPARE(entry.value(QStringLiteral("installedVersion")).toString(), QStringLiteral("1.2.0"));
}

void PluginCatalogManagerTest::_hashMismatchRefused_test()
{
    const QString id = QStringLiteral("org.test.catalog.hash");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("hash.qgcplugin"), zip);
    // Same size, so only the hash can tell the bytes apart
    QByteArray other = zip;
    other[other.size() - 1] = static_cast<char>(other.at(other.size() - 1) ^ 0x01);
    (void) _setCatalog(
        {_pluginJson(id, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, other)})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    const QStringList pluginsBefore = _userPluginsEntries();

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Install of .* failed")));
    const QString stagingDir = _startInstall(catalog, id);
    QVERIFY(!stagingDir.isEmpty());
    QVERIFY_SIGNAL_WAIT(spy, TestTimeout::mediumMs());
    verifyExpectedLogMessage();

    _verifyRefused(spy, id, QStringLiteral("Hash verification failed"), stagingDir, pluginsBefore);
}

void PluginCatalogManagerTest::_missingPackageRefused_test()
{
    const QString id = QStringLiteral("org.test.catalog.missing");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.0.0"));
    (void) _setCatalog({_pluginJson(id, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"),
                                                      tempPath(QStringLiteral("gone.qgcplugin")), zip)})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    const QStringList pluginsBefore = _userPluginsEntries();

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    expectLogMessage(kDownloadLog, QtWarningMsg, QRegularExpression(QStringLiteral("Download error")));
    expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Install of .* failed")));
    const QString stagingDir = _startInstall(catalog, id);
    QVERIFY(!stagingDir.isEmpty());
    QVERIFY_SIGNAL_WAIT(spy, TestTimeout::mediumMs());
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();

    _verifyRefused(spy, id, QStringLiteral("Download failed"), stagingDir, pluginsBefore);
}

void PluginCatalogManagerTest::_manifestMismatchRefused_test_data()
{
    QTest::addColumn<QString>("zipId");
    QTest::addColumn<QString>("zipVersion");
    QTest::addColumn<QString>("zipTier");
    QTest::addColumn<QString>("errorPattern");

    const QString id = QStringLiteral("org.test.catalog.entry");
    QTest::newRow("id") << QStringLiteral("org.test.catalog.other") << QStringLiteral("1.0.0") << QStringLiteral("qml")
                        << QStringLiteral("declares id org.test.catalog.other");
    QTest::newRow("version") << id << QStringLiteral("1.0.1") << QStringLiteral("qml")
                             << QStringLiteral("declares version 1.0.1");
    QTest::newRow("trailing zero version")
        << id << QStringLiteral("1.0") << QStringLiteral("qml") << QStringLiteral("declares version 1.0,");
    QTest::newRow("tier") << id << QStringLiteral("1.0.0") << QStringLiteral("sdk")
                          << QStringLiteral("declares tier sdk");
}

void PluginCatalogManagerTest::_manifestMismatchRefused_test()
{
    QFETCH(QString, zipId);
    QFETCH(QString, zipVersion);
    QFETCH(QString, zipTier);
    QFETCH(QString, errorPattern);

    // The hash matches: these are exactly the bytes the index names, but not the plugin it names
    const QString id = QStringLiteral("org.test.catalog.entry");
    const QByteArray zip = _packageBytes(zipId, zipVersion, zipTier);
    const QString zipPath = _writeFile(QStringLiteral("mismatch.qgcplugin"), zip);
    (void) _setCatalog({_pluginJson(id, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip)})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    const QStringList pluginsBefore = _userPluginsEntries();

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Install of .* failed")));
    const QString stagingDir = _startInstall(catalog, id);
    QVERIFY(!stagingDir.isEmpty());
    QVERIFY_SIGNAL_WAIT(spy, TestTimeout::mediumMs());
    verifyExpectedLogMessage();

    _verifyRefused(spy, id, errorPattern, stagingDir, pluginsBefore);
    QVERIFY(!QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(zipId)));
}

void PluginCatalogManagerTest::_oversizedPackageRefused_test()
{
    const QString id = QStringLiteral("org.test.catalog.size");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("size.qgcplugin"), zip);
    // The hash is right; only the listed size is short
    QJsonObject version = _versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip);
    QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
    QJsonObject package = packages.value(QStringLiteral("any")).toObject();
    package[QStringLiteral("size")] = static_cast<double>(zip.size() - 1);
    packages[QStringLiteral("any")] = package;
    version[QStringLiteral("packages")] = packages;
    (void) _setCatalog({_pluginJson(id, {version})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    const QStringList pluginsBefore = _userPluginsEntries();

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Install of .* failed")));
    const QString stagingDir = _startInstall(catalog, id);
    QVERIFY(!stagingDir.isEmpty());
    QVERIFY_SIGNAL_WAIT(spy, TestTimeout::mediumMs());
    verifyExpectedLogMessage();

    _verifyRefused(spy, id, QStringLiteral("larger than the %1 bytes").arg(zip.size() - 1), stagingDir, pluginsBefore);
}

void PluginCatalogManagerTest::_updateIsStaged_test()
{
    const QString id = QStringLiteral("org.test.catalog.update");
    const QByteArray v1 = _packageBytes(id, QStringLiteral("1.0.0"));
    const QByteArray v2 = _packageBytes(id, QStringLiteral("2.0.0"));
    const QString v1Path = _writeFile(QStringLiteral("update-v1.qgcplugin"), v1);
    const QString v2Path = _writeFile(QStringLiteral("update-v2.qgcplugin"), v2);
    (void) _setCatalog({_pluginJson(id, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), v1Path, v1),
                                         _versionJson(QStringLiteral("2.0.0"), QStringLiteral("any"), v2Path, v2)})});

    QGCPluginManager pluginManager;
    QCOMPARE(pluginManager.installPlugin(v1Path), QString());
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));

    QCOMPARE(catalog.availableUpdates().size(), 1);
    const QVariantMap update = catalog.availableUpdates().first().toMap();
    QCOMPARE(update.value(QStringLiteral("id")).toString(), id);
    QCOMPARE(update.value(QStringLiteral("installedVersion")).toString(), QStringLiteral("1.0.0"));
    QCOMPARE(update.value(QStringLiteral("version")).toString(), QStringLiteral("2.0.0"));

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    const QString stagingDir = _startInstall(catalog, id);
    QVERIFY(!stagingDir.isEmpty());
    QVERIFY_SIGNAL_WAIT(spy, TestTimeout::mediumMs());
    QCOMPARE(spy.first().at(1).toBool(), true);
    QCOMPARE(spy.first().at(2).toString(), QString());
    QCOMPARE(spy.first().at(3).toBool(), true);
    QVERIFY(!QFileInfo::exists(stagingDir));

    // Staged for the next start; the running copy is untouched
    QVERIFY(QFileInfo::exists(QDir(PluginInstaller::pendingUpdateDir(id)).filePath(QStringLiteral("qgcplugin.json"))));
    const QVariantMap known = knownPlugin(pluginManager, id);
    QCOMPARE(known.value(QStringLiteral("version")).toString(), QStringLiteral("1.0.0"));
    QCOMPARE(known.value(QStringLiteral("state")).toString(), QStringLiteral("Active"));
    QCOMPARE(known.value(QStringLiteral("updateStaged")).toBool(), true);

    const QVariantMap entry = catalog.entries().first().toMap();
    QCOMPARE(entry.value(QStringLiteral("updateStaged")).toBool(), true);
    QCOMPARE(entry.value(QStringLiteral("updateAvailable")).toBool(), false);
    QVERIFY(catalog.availableUpdates().isEmpty());
}

void PluginCatalogManagerTest::_installRefusedBeforeDownload_test_data()
{
    QTest::addColumn<QString>("caseName");
    QTest::addColumn<QString>("errorPattern");

    QTest::newRow("unknown id") << QStringLiteral("unknown") << QStringLiteral("does not list");
    QTest::newRow("no compatible version") << QStringLiteral("incompatible") << QStringLiteral("No version of");
    QTest::newRow("http package") << QStringLiteral("http") << QStringLiteral("must come from an https:// URL");
    QTest::newRow("already current") << QStringLiteral("current") << QStringLiteral("nothing newer");
}

void PluginCatalogManagerTest::_installRefusedBeforeDownload_test()
{
    QFETCH(QString, caseName);
    QFETCH(QString, errorPattern);

    const QString id = QStringLiteral("org.test.catalog.refused");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("refused.qgcplugin"), zip);
    QJsonObject version = _versionJson(
        QStringLiteral("1.0.0"), QStringLiteral("any"),
        caseName == QStringLiteral("http") ? QStringLiteral("http://example.com/refused.qgcplugin") : zipPath, zip);
    if (caseName == QStringLiteral("incompatible")) {
        QJsonObject hostVersion;
        hostVersion[QStringLiteral("min")] = QStringLiteral("999.0");
        version[QStringLiteral("hostVersion")] = hostVersion;
    }
    (void) _setCatalog({_pluginJson(id, {version})});

    QGCPluginManager pluginManager;
    if (caseName == QStringLiteral("current")) {
        QCOMPARE(pluginManager.installPlugin(zipPath), QString());
    }
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    const QStringList pluginsBefore = _userPluginsEntries();

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    if (caseName == QStringLiteral("http")) {
        expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Refusing package URL")));
    }
    const QString error =
        catalog.install(caseName == QStringLiteral("unknown") ? QStringLiteral("org.test.catalog.none") : id);
    if (caseName == QStringLiteral("http")) {
        verifyExpectedLogMessage();
    }

    QVERIFY2(QRegularExpression(errorPattern).match(error).hasMatch(), qPrintable(error));
    QVERIFY(catalog.installingId().isEmpty());
    QVERIFY(!catalog._installDir);
    QVERIFY(!catalog._packageDownload);
    QCOMPARE(_userPluginsEntries(), pluginsBefore);
    // Refusals are synchronous and final; no installFinished follows. A literal window:
    // proving absence always burns the whole timeout, so it stays short.
    QVERIFY_NO_SIGNAL_WAIT(spy, 100);
}

void PluginCatalogManagerTest::_overlappingInstallRefused_test()
{
    const QString firstId = QStringLiteral("org.test.catalog.firstinstall");
    const QString secondId = QStringLiteral("org.test.catalog.secondinstall");
    const QByteArray firstZip = _packageBytes(firstId, QStringLiteral("1.0.0"));
    const QByteArray secondZip = _packageBytes(secondId, QStringLiteral("1.0.0"));
    const QString firstPath = _writeFile(QStringLiteral("firstinstall.qgcplugin"), firstZip);
    const QString secondPath = _writeFile(QStringLiteral("secondinstall.qgcplugin"), secondZip);
    (void) _setCatalog(
        {_pluginJson(firstId, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), firstPath, firstZip)}),
         _pluginJson(secondId, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), secondPath, secondZip)})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    const QString stagingDir = _startInstall(catalog, firstId);
    QVERIFY(!stagingDir.isEmpty());

    // Neither a second plugin nor a repeat of the first starts while one runs
    QVERIFY(catalog.install(secondId).contains(QStringLiteral("still being installed")));
    QVERIFY(catalog.install(firstId).contains(QStringLiteral("still being installed")));
    QCOMPARE(catalog.installingId(), firstId);
    QCOMPARE(catalog._installDir->path(), stagingDir);

    QVERIFY_SIGNAL_WAIT(spy, TestTimeout::mediumMs());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), firstId);
    QCOMPARE(spy.first().at(1).toBool(), true);

    // Once it is done the next one runs normally
    QCOMPARE(catalog.install(secondId), QString());
    QVERIFY_SIGNAL_COUNT_WAIT(spy, 2, TestTimeout::mediumMs());
    QCOMPARE(spy.at(1).at(0).toString(), secondId);
    QCOMPARE(spy.at(1).at(1).toBool(), true);
    QVERIFY(QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(firstId)));
    QVERIFY(QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(secondId)));
}

void PluginCatalogManagerTest::_staleCacheNeverStandsInForNetwork_test()
{
    // Loopback port 1 refuses at once, so every network read of this URL fails.
    const QString url = QStringLiteral("https://127.0.0.1:1/index.json");
    const QString id = QStringLiteral("org.test.catalog.cached");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.0.0"));
    QJsonObject index;
    index[QStringLiteral("schemaVersion")] = 1;
    index[QStringLiteral("plugins")] = QJsonArray{_pluginJson(
        id,
        {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), QStringLiteral("https://example.com/p"), zip)})};
    const QByteArray indexBytes = QJsonDocument(index).toJson();

    // A copy of the index as an earlier fetch would have cached it, stamped now. Qt serves a
    // PreferCache read only from an item that carries response headers and has not expired,
    // as a real response with Cache-Control max-age has; without both, every read here would
    // go to the network.
    {
        QNetworkDiskCache seed;
        seed.setCacheDirectory(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                   .filePath(QStringLiteral("PluginCatalog")));
        QNetworkCacheMetaData metaData;
        metaData.setUrl(QUrl(url));
        metaData.setSaveToDisk(true);
        metaData.setRawHeaders({{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")}});
        metaData.setExpirationDate(QDateTime::currentDateTimeUtc().addSecs(60 * 60));
        QNetworkCacheMetaData::AttributesMap attributes;
        attributes.insert(QNetworkRequest::HttpStatusCodeAttribute, 200);
        attributes.insert(QNetworkRequest::User, QDateTime::currentDateTime());
        metaData.setAttributes(attributes);
        QIODevice* const device = seed.prepare(metaData);
        QVERIFY(device);
        QCOMPARE(device->write(indexBytes), indexBytes.size());
        seed.insert(device);
    }

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    catalogUrlFact()->setRawValue(url);

    // Fresh: read from the cache with no network attempt
    QVERIFY(_fetch(catalog));
    QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Ready);
    const QVariantList entriesBefore = catalog.entries();
    QCOMPARE(entriesBefore.size(), 1);

    // Expired: the network read fails, and the old copy must not stand in for it
    QNetworkDiskCache* const cache = catalog._indexDownload->diskCache();
    QNetworkCacheMetaData metaData = cache->metaData(QUrl(url));
    QVERIFY(metaData.isValid());
    QNetworkCacheMetaData::AttributesMap attributes = metaData.attributes();
    attributes.insert(QNetworkRequest::User, QDateTime::currentDateTime().addDays(-1));
    metaData.setAttributes(attributes);
    cache->updateMetaData(metaData);
    QVERIFY(!catalog._indexDownload->isCached(url, 60));

    for (int attempt = 0; attempt < 2; ++attempt) {
        // The second pass is the fetch that would read a copy the first one stamped fresh
        expectLogMessage(kDownloadLog, QtWarningMsg, QRegularExpression(QStringLiteral("Connection refused")));
        expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Could not read the catalog")));
        QVERIFY(_fetch(catalog));
        verifyExpectedLogMessage();
        verifyExpectedLogMessage();

        QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Failed);
        QVERIFY2(catalog.fetchError().contains(QStringLiteral("Connection refused")), qPrintable(catalog.fetchError()));
        QCOMPARE(catalog.entries(), entriesBefore);
    }
}

void PluginCatalogManagerTest::_destroyedMidFetchLeavesNothing_test()
{
    const QString id = QStringLiteral("org.test.catalog.midfetch");
    const QByteArray zip = _packageBytes(id, QStringLiteral("1.0.0"));
    const QString zipPath = _writeFile(QStringLiteral("midfetch.qgcplugin"), zip);
    (void) _setCatalog({_pluginJson(id, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip)})});

    QGCPluginManager pluginManager;
    QString indexDir;
    QString stagingDir;
    {
        PluginCatalogManager catalog(&pluginManager);
        QVERIFY(_fetch(catalog));
        stagingDir = _startInstall(catalog, id);
        QVERIFY(!stagingDir.isEmpty());
        catalog.fetch();
        QCOMPARE(catalog.fetchState(), PluginCatalogManager::FetchState::Fetching);
        QVERIFY(catalog._indexDir);
        indexDir = catalog._indexDir->path();
        QVERIFY(QFileInfo::exists(indexDir));
    }

    // Both downloads are torn down with their files closed, and both directories go
    QVERIFY2(!QFileInfo::exists(indexDir), qPrintable(indexDir));
    QVERIFY2(!QFileInfo::exists(stagingDir), qPrintable(stagingDir));
    QVERIFY(!QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(id)));
}

void PluginCatalogManagerTest::_invalidPackageRefused_test()
{
    // The hash matches, but the bytes are not a package: the installer's own archive
    // checks refuse it before anything is extracted
    const QString id = QStringLiteral("org.test.catalog.invalid");
    const QByteArray zip =
        storedZipArchive({{QStringLiteral("qml/View.qml"), QByteArrayLiteral("import QtQuick\nItem {}\n")}});
    const QString zipPath = _writeFile(QStringLiteral("invalid.qgcplugin"), zip);
    (void) _setCatalog({_pluginJson(id, {_versionJson(QStringLiteral("1.0.0"), QStringLiteral("any"), zipPath, zip)})});

    QGCPluginManager pluginManager;
    PluginCatalogManager catalog(&pluginManager);
    QVERIFY(_fetch(catalog));
    const QStringList pluginsBefore = _userPluginsEntries();

    QSignalSpy spy(&catalog, &PluginCatalogManager::installFinished);
    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg,
                     QRegularExpression(QStringLiteral("archive does not contain qgcplugin.json")));
    expectLogMessage(kCatalogLog, QtWarningMsg, QRegularExpression(QStringLiteral("Install of .* failed")));
    const QString stagingDir = _startInstall(catalog, id);
    QVERIFY(!stagingDir.isEmpty());
    QVERIFY_SIGNAL_WAIT(spy, TestTimeout::mediumMs());
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();

    _verifyRefused(spy, id, QStringLiteral("The package is invalid: archive does not contain qgcplugin.json"),
                   stagingDir, pluginsBefore);
}

UT_REGISTER_TEST(PluginCatalogManagerTest, TestLabel::Unit)
