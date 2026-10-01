#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include "BaseClasses/TempDirectoryTest.h"

class PluginCatalogManager;
class QSignalSpy;

/// TempDirectoryTest: serves index.json and .qgcplugin fixtures from the temp directory
/// as local paths and file URLs, and installs into a test-scoped user plugins directory.
class PluginCatalogManagerTest : public TempDirectoryTest
{
    Q_OBJECT

private slots:
    void init() override;
    void cleanup() override;

    void _emptyUrlTurnsCatalogOff_test();
    void _fetchListsEntries_test();
    void _unreachableUrlChangesNothing_test_data();
    void _unreachableUrlChangesNothing_test();
    void _newerSchemaListsNothing_test();
    void _urlChangedDuringFetchFetchesAgain_test();
    void _staleCacheNeverStandsInForNetwork_test();
    void _destroyedMidFetchLeavesNothing_test();
    void _sourcePolicy_test_data();
    void _sourcePolicy_test();

    void _goodPackageInstalls_test();
    void _hashMismatchRefused_test();
    void _missingPackageRefused_test();
    void _manifestMismatchRefused_test_data();
    void _manifestMismatchRefused_test();
    void _oversizedPackageRefused_test();
    void _invalidPackageRefused_test();
    void _updateIsStaged_test();
    void _installRefusedBeforeDownload_test_data();
    void _installRefusedBeforeDownload_test();
    void _overlappingInstallRefused_test();

private:
    // A .qgcplugin zip whose manifest declares id, version and tier.
    static QByteArray _packageBytes(const QString& id, const QString& version,
                                    const QString& tier = QStringLiteral("qml"));
    static QString _sha256Hex(const QByteArray& bytes);
    static QJsonObject _versionJson(const QString& version, const QString& packageKey, const QString& url,
                                    const QByteArray& packageBytes, const QString& tier = QStringLiteral("qml"));
    static QJsonObject _pluginJson(const QString& id, const QJsonArray& versions);
    // Every name in the user plugins directory, staging directories included.
    static QStringList _userPluginsEntries();

    QString _writeFile(const QString& name, const QByteArray& bytes);
    // Writes an index listing plugins to name and points catalogUrl at it.
    QString _setCatalog(const QJsonArray& plugins, const QString& name = QStringLiteral("index.json"),
                        int schemaVersion = 1);
    // Fetches and waits for the fetch to end.
    bool _fetch(PluginCatalogManager& catalog);
    // Starts install(id); returns the staging directory the download writes into, or empty
    // if install() refused to start.
    static QString _startInstall(PluginCatalogManager& catalog, const QString& id);
    // The install failed with an error matching errorPattern, and left nothing behind.
    void _verifyRefused(const QSignalSpy& spy, const QString& id, const QString& errorPattern,
                        const QString& stagingDir, const QStringList& pluginsBefore);
};
