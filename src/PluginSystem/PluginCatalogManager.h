/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <memory>

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

#include "PluginCatalog.h"

Q_DECLARE_LOGGING_CATEGORY(PluginCatalogManagerLog)

class QGCCachedFileDownload;
class QGCFileDownload;
class QGCPluginManager;
class QJSEngine;
class QQmlEngine;
class QTemporaryDir;

/// Reads the plugin catalog index from PluginSettings::catalogUrl and installs or updates
/// plugins from it.
///
/// A package is downloaded to a temporary directory outside the user plugins directory and
/// checked against the index's SHA-256 and size. Its qgcplugin.json must declare the
/// entry's id, version and tier. Only then does it go to QGCPluginManager: installPlugin()
/// for a plugin that is not installed, stagePluginUpdate() for one that is. The temporary
/// directory is deleted whatever the outcome.
///
/// URL policy: an https URL is always accepted, plain http never. A local path or file URL
/// is accepted for the index, and for a package only when the index itself was read from a
/// local path, so a remote index cannot point the installer at a file on this machine.
///
/// One install runs at a time. A fetch requested while one is running is not started; when
/// the running one finishes, the index is fetched again if catalogUrl changed meanwhile.
class PluginCatalogManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(FetchState fetchState READ fetchState NOTIFY fetchStateChanged)
    Q_PROPERTY(QString fetchError READ fetchError NOTIFY fetchStateChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QVariantList availableUpdates READ availableUpdates NOTIFY entriesChanged)
    Q_PROPERTY(QString installingId READ installingId NOTIFY installingIdChanged)
    Q_PROPERTY(qreal installProgress READ installProgress NOTIFY installProgressChanged)

public:
    enum class FetchState
    {
        Idle,    ///< Never fetched, or catalogUrl is empty
        Fetching,
        Ready,   ///< entries holds the index last read
        Failed,  ///< fetchError says why
    };
    Q_ENUM(FetchState)

    /// There is deliberately no default constructor: QML prefers one over create() and
    /// would build a second instance.
    explicit PluginCatalogManager(QGCPluginManager* pluginManager, QObject* parent = nullptr);
    ~PluginCatalogManager() override;

    static PluginCatalogManager* instance();
    static PluginCatalogManager* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    FetchState fetchState() const { return _fetchState; }

    QString fetchError() const { return _fetchError; }

    /// One QVariantMap per catalog plugin, keys: id, name, author, summary, description,
    /// license, homepage, repository, icon, screenshots (list), compatible (a version runs
    /// on this host and platform), version, tier, released, notes, size (of that version),
    /// installed, installedVersion, updateAvailable, updateStaged.
    QVariantList entries() const { return _entries; }

    /// The entries with updateAvailable set, as maps with keys id, name, installedVersion,
    /// version.
    QVariantList availableUpdates() const { return _availableUpdates; }

    /// The plugin being downloaded or installed, or empty.
    QString installingId() const { return _installingId; }

    /// Download progress of the running install, 0 to 1.
    qreal installProgress() const { return _installProgress; }

    /// Fetch the index from catalogUrl. An empty URL turns the catalog off: nothing is read,
    /// and any entries from an earlier fetch are dropped. A URL that cannot be read sets
    /// Failed and keeps the entries; an index that is read but refused (malformed, or a
    /// newer schema) sets Failed and drops them. A copy read less than ten minutes ago, from
    /// the network or the cache, and not yet expired by the server's cache headers, is read
    /// without the network; an older copy is never used in place of a failed read.
    Q_INVOKABLE void fetch();

    /// Install the newest version of pluginId that runs here, or stage it as an update when
    /// an older version is installed. The user's request is the consent to run it.
    /// @return Empty if a download started; installFinished() follows exactly once.
    ///         Otherwise why nothing started, and no signal follows.
    Q_INVOKABLE QString install(const QString& pluginId);

signals:
    void fetchStateChanged();
    void entriesChanged();
    void installingIdChanged();
    void installProgressChanged();

    /// @param staged true when the package waits for a restart (an update)
    void installFinished(const QString& pluginId, bool success, const QString& errorString, bool staged);

private:
    /// Where an index or package may be read from (see the class comment).
    static bool _isAllowedSource(const QString& location, bool allowLocal);
    static bool _isLocalSource(const QString& location);

    QString _catalogUrl() const;
    void _setFetchState(FetchState state, const QString& error = QString());
    void _startFetch();
    void _onIndexFinished(bool success, const QString& localPath, QString errorMessage, bool fromCache);
    void _rebuildEntries();
    void _setInstallProgress(qreal progress);
    void _onPackageProgress(qint64 bytesReceived);
    void _onPackageFinished(QGCFileDownload* download, bool success, const QString& errorMessage);
    /// Checks the downloaded package against the offer and hands it to QGCPluginManager.
    /// @return Empty on success, otherwise why it was refused
    QString _installDownloaded(const QString& zipPath);
    void _finishInstall(bool success, const QString& errorString);

    QPointer<QGCPluginManager> _pluginManager;

    PluginCatalog _catalog;
    bool _catalogIsLocal = false;  ///< The index was read from a local path or file URL
    QVariantList _entries;
    QVariantList _availableUpdates;
    FetchState _fetchState = FetchState::Idle;
    QString _fetchError;

    QGCCachedFileDownload* _indexDownload = nullptr;
    std::unique_ptr<QTemporaryDir> _indexDir;
    QString _fetchingUrl;
    bool _fetchNeedsNetwork = false;  ///< No unexpired cached copy when the fetch started
    QString _lastIndexError;          ///< The running fetch's last network error

    /// The running install. The offer is a copy, so a fetch that replaces _catalog meanwhile
    /// cannot change what is being checked.
    QString _installingId;
    PluginCatalogOffer _installOffer;
    bool _installIsUpdate = false;
    QGCFileDownload* _packageDownload = nullptr;
    std::unique_ptr<QTemporaryDir> _installDir;
    QString _installFailure;  ///< Set before cancelling a download, to report instead of "cancelled"
    qreal _installProgress = 0.0;

    friend class PluginCatalogManagerTest;
};
