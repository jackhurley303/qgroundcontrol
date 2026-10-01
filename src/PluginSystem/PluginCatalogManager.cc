/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginCatalogManager.h"

#include <algorithm>
#include <type_traits>

#include <QtCore/QApplicationStatic>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtQml/QJSEngine>

#include "Fact.h"
#include "PluginInstaller.h"
#include "PluginSettings.h"
#include "QGCCachedFileDownload.h"
#include "QGCFileDownload.h"
#include "QGCLoggingCategory.h"
#include "QGCPluginLoader.h"
#include "QGCPluginManager.h"
#include "SettingsManager.h"

QGC_LOGGING_CATEGORY(PluginCatalogManagerLog, "PluginSystem.PluginCatalogManager")

static_assert(!std::is_default_constructible_v<PluginCatalogManager>,
              "a default constructor makes QML build a second instance instead of calling create()");

Q_APPLICATION_STATIC(PluginCatalogManager, _pluginCatalogManagerInstance, QGCPluginManager::instance());

namespace {

// Repeat visits to the Plugins page within this window read the cached index.
constexpr int kIndexMaxAgeSec = 10 * 60;

constexpr const char* kIndexFileName = "index.json";
constexpr const char* kPackageFileName = "package.qgcplugin";

// The installed plugins, by id, in knownPlugins() form.
QHash<QString, QVariantMap> knownPluginsById(const QGCPluginManager* pluginManager)
{
    QHash<QString, QVariantMap> known;
    if (!pluginManager) {
        return known;
    }
    for (const QVariant& value : pluginManager->knownPlugins()) {
        const QVariantMap info = value.toMap();
        const QString id = info.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            known.insert(id, info);
        }
    }
    return known;
}

}  // namespace

PluginCatalogManager::PluginCatalogManager(QGCPluginManager* pluginManager, QObject* parent)
    : QObject(parent)
    , _pluginManager(pluginManager)
{
    if (_pluginManager) {
        // Installs, removals and staged updates change what each entry offers.
        (void) connect(_pluginManager, &QGCPluginManager::loadedPluginsChanged, this,
                       &PluginCatalogManager::_rebuildEntries);
    }
}

PluginCatalogManager::~PluginCatalogManager()
{
    // Close each download's file before _indexDir and _installDir delete the directories
    // they are in: the members go before Qt deletes the children, and Windows cannot delete
    // an open file.
    if (_indexDownload) {
        _indexDownload->disconnect(this);
        _indexDownload->fileDownloader()->disconnect(this);
        delete _indexDownload;
        _indexDownload = nullptr;
    }
    if (_packageDownload) {
        _packageDownload->disconnect(this);
        delete _packageDownload;
        _packageDownload = nullptr;
    }
}

PluginCatalogManager* PluginCatalogManager::instance()
{
    return _pluginCatalogManagerInstance();
}

PluginCatalogManager* PluginCatalogManager::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);
    PluginCatalogManager* const manager = instance();
    QJSEngine::setObjectOwnership(manager, QJSEngine::CppOwnership);
    return manager;
}

bool PluginCatalogManager::_isLocalSource(const QString& location)
{
    // ":/" is a resource path, which QDir counts as absolute; nothing serves a catalog there.
    return QUrl(location).isLocalFile() || (QDir::isAbsolutePath(location) && !location.startsWith(QLatin1Char(':')));
}

bool PluginCatalogManager::_isAllowedSource(const QString& location, bool allowLocal)
{
    if (_isLocalSource(location)) {
        return allowLocal;
    }
    // Lowercase only: QGCFileDownload::start() treats anything not starting "https:" as a
    // local path.
    const QUrl url(location, QUrl::StrictMode);
    return location.startsWith(QLatin1String("https://")) && url.isValid() && !url.host().isEmpty();
}

QString PluginCatalogManager::_catalogUrl() const
{
    return SettingsManager::instance()->pluginSettings()->catalogUrl()->rawValue().toString().trimmed();
}

void PluginCatalogManager::_setFetchState(FetchState state, const QString& error)
{
    if (_fetchState == state && _fetchError == error) {
        return;
    }
    _fetchState = state;
    _fetchError = error;
    emit fetchStateChanged();
}

void PluginCatalogManager::fetch()
{
    if (_fetchState == FetchState::Fetching) {
        qCDebug(PluginCatalogManagerLog) << "Fetch already running";
        return;
    }
    _startFetch();
}

void PluginCatalogManager::_startFetch()
{
    const QString url = _catalogUrl();
    if (url.isEmpty()) {
        qCDebug(PluginCatalogManagerLog) << "Catalog URL is empty; the catalog is off";
        if (!_catalog.plugins.isEmpty()) {
            _catalog = PluginCatalog();
            _rebuildEntries();
        }
        _setFetchState(FetchState::Idle);
        return;
    }

    if (!_isAllowedSource(url, true)) {
        qCWarning(PluginCatalogManagerLog) << "Refusing catalog URL" << url;
        _setFetchState(FetchState::Failed, tr("The catalog URL must start with https://, or name a local file"));
        return;
    }

    if (!_indexDownload) {
        const QString cacheDir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                     .filePath(QStringLiteral("PluginCatalog"));
        _indexDownload = new QGCCachedFileDownload(cacheDir, this);
        // Queued, so the handler can start the next fetch outside the downloader's own emission.
        (void) connect(
            _indexDownload, &QGCCachedFileDownload::finished, this,
            [this](bool success, const QString& localPath, const QString& errorMessage, bool fromCache) {
                _onIndexFinished(success, localPath, errorMessage, fromCache);
            },
            Qt::QueuedConnection);
        // QGCCachedFileDownload drops the network's error when it falls back to its cache.
        (void) connect(_indexDownload->fileDownloader(), &QGCFileDownload::finished, this,
                       [this](bool success, const QString&, const QString& errorMessage) {
                           if (!success) {
                               _lastIndexError = errorMessage;
                           }
                       });
    }

    _indexDir = std::make_unique<QTemporaryDir>();
    if (!_indexDir->isValid()) {
        qCWarning(PluginCatalogManagerLog) << "Could not create a temporary directory:" << _indexDir->errorString();
        _setFetchState(FetchState::Failed,
                       tr("Could not create a temporary directory: %1").arg(_indexDir->errorString()));
        _indexDir.reset();
        return;
    }
    _indexDownload->fileDownloader()->setOutputPath(_indexDir->filePath(QString::fromLatin1(kIndexFileName)));

    qCDebug(PluginCatalogManagerLog) << "Fetching catalog" << url;
    _fetchingUrl = url;
    _lastIndexError.clear();
    _setFetchState(FetchState::Fetching);

    // Decided here, not left to download(): when the network fails, download() answers from
    // an expired copy and stamps that copy fresh, so a host that is down would keep serving
    // its old index as current. A fresh copy is read without the network; otherwise any
    // answer from the cache is the network failure it stands in for. That includes a
    // fallback read the server answers with 304, which also arrives as a cache answer: it
    // reports one Failed and drops a good copy, and the next fetch reads the network. That
    // costs less than letting a down host's old copy pass as current.
    _fetchNeedsNetwork = !_indexDownload->isCached(url, kIndexMaxAgeSec);
    const bool started =
        _fetchNeedsNetwork ? _indexDownload->download(url, kIndexMaxAgeSec) : _indexDownload->downloadPreferCache(url);
    if (!started) {
        const QString error = _indexDownload->errorString();
        qCWarning(PluginCatalogManagerLog) << "Could not start the catalog download:" << error;
        _fetchingUrl.clear();
        _indexDir.reset();
        _setFetchState(FetchState::Failed, tr("Could not read the plugin catalog: %1").arg(error));
    }
}

void PluginCatalogManager::_onIndexFinished(bool success, const QString& localPath, QString errorMessage,
                                            bool fromCache)
{
    const QString fetchedUrl = _fetchingUrl;
    _fetchingUrl.clear();

    if (success && fromCache && _fetchNeedsNetwork) {
        // The expired copy, now stamped fresh. Drop it, or the next fetch reads it as current.
        (void) _indexDownload->removeFromCache(fetchedUrl);
        success = false;
        errorMessage = _lastIndexError.isEmpty() ? tr("The catalog could not be reached") : _lastIndexError;
    }

    QByteArray data;
    if (success) {
        QFile file(localPath);
        if (file.open(QIODevice::ReadOnly)) {
            data = file.readAll();
        }
    }
    _indexDir.reset();

    // The URL changed while this fetch ran: its result describes a catalog no longer asked for.
    if (fetchedUrl != _catalogUrl()) {
        qCDebug(PluginCatalogManagerLog) << "Catalog URL changed during the fetch; fetching again";
        _startFetch();
        return;
    }

    if (!success) {
        qCWarning(PluginCatalogManagerLog) << "Could not read the catalog" << fetchedUrl << "-" << errorMessage;
        _setFetchState(FetchState::Failed, tr("Could not read the plugin catalog: %1").arg(errorMessage));
        return;
    }

    QString error;
    const PluginCatalog catalog = PluginCatalog::fromData(data, &error);
    if (catalog.needsNewerHost()) {
        qCWarning(PluginCatalogManagerLog) << "Refusing the catalog" << fetchedUrl << "-" << error;
        _catalog = PluginCatalog();
        _rebuildEntries();
        _setFetchState(FetchState::Failed, tr("This catalog needs a newer QGroundControl"));
        return;
    }
    if (!error.isEmpty()) {
        qCWarning(PluginCatalogManagerLog) << "Refusing the catalog" << fetchedUrl << "-" << error;
        _catalog = PluginCatalog();
        _rebuildEntries();
        _setFetchState(FetchState::Failed, tr("The plugin catalog is invalid: %1").arg(error));
        return;
    }

    qCDebug(PluginCatalogManagerLog) << "Catalog" << fetchedUrl << "lists" << catalog.plugins.size() << "plugin(s)";
    _catalog = catalog;
    _catalogIsLocal = _isLocalSource(fetchedUrl);
    _rebuildEntries();
    _setFetchState(FetchState::Ready);
}

void PluginCatalogManager::_rebuildEntries()
{
    const QHash<QString, QVariantMap> known = knownPluginsById(_pluginManager);
    const HostInfo host = QGCPluginLoader::hostInfo();
    const QString platformKey = QGCPluginLoader::platformKey();

    QVariantList entries;
    QVariantList updates;
    for (const PluginCatalogEntry& entry : _catalog.plugins) {
        QVariantMap item;
        item[QStringLiteral("id")] = entry.id;
        item[QStringLiteral("name")] = entry.name;
        item[QStringLiteral("author")] = entry.author;
        item[QStringLiteral("summary")] = entry.summary;
        item[QStringLiteral("description")] = entry.description;
        item[QStringLiteral("license")] = entry.license;
        item[QStringLiteral("homepage")] = entry.homepage;
        item[QStringLiteral("repository")] = entry.repository;
        item[QStringLiteral("icon")] = entry.icon;
        item[QStringLiteral("screenshots")] = entry.screenshots;

        const std::optional<PluginCatalogOffer> offer = entry.newestCompatible(host, platformKey);
        item[QStringLiteral("compatible")] = offer.has_value();
        if (offer) {
            item[QStringLiteral("version")] = offer->version.manifest.version.toString();
            item[QStringLiteral("tier")] = PluginManifest::tierToString(offer->version.manifest.tier);
            item[QStringLiteral("released")] = offer->version.released;
            item[QStringLiteral("notes")] = offer->version.notes;
            item[QStringLiteral("size")] = offer->package.size;
        }

        const auto installed = known.constFind(entry.id);
        const bool isInstalled = installed != known.constEnd();
        const QString installedVersion =
            isInstalled ? installed->value(QStringLiteral("version")).toString() : QString();
        const bool updateStaged = isInstalled && installed->value(QStringLiteral("updateStaged")).toBool();
        // A staged update shows as "restart to finish", not as another update to download.
        const bool updateAvailable =
            isInstalled && !updateStaged &&
            entry.updateFor(host, platformKey, QVersionNumber::fromString(installedVersion)).has_value();
        item[QStringLiteral("installed")] = isInstalled;
        item[QStringLiteral("installedVersion")] = installedVersion;
        item[QStringLiteral("updateStaged")] = updateStaged;
        item[QStringLiteral("updateAvailable")] = updateAvailable;
        entries.append(item);

        if (updateAvailable) {
            QVariantMap update;
            update[QStringLiteral("id")] = entry.id;
            update[QStringLiteral("name")] = entry.name;
            update[QStringLiteral("installedVersion")] = installedVersion;
            update[QStringLiteral("version")] = item.value(QStringLiteral("version"));
            updates.append(update);
        }
    }

    _entries = entries;
    _availableUpdates = updates;
    emit entriesChanged();
}

QString PluginCatalogManager::install(const QString& pluginId)
{
    if (!_installingId.isEmpty()) {
        return tr("%1 is still being installed").arg(_installingId);
    }
    if (!_pluginManager) {
        return tr("Plugins are not available");
    }

    const PluginCatalogEntry* const entry = _catalog.find(pluginId);
    if (!entry) {
        return tr("The catalog does not list %1").arg(pluginId);
    }

    const HostInfo host = QGCPluginLoader::hostInfo();
    const QString platformKey = QGCPluginLoader::platformKey();
    const std::optional<PluginCatalogOffer> offer = entry->newestCompatible(host, platformKey);
    if (!offer) {
        return tr("No version of %1 runs on this QGroundControl").arg(entry->name);
    }

    const QHash<QString, QVariantMap> known = knownPluginsById(_pluginManager);
    const auto installed = known.constFind(pluginId);
    const bool isUpdate = installed != known.constEnd();
    if (isUpdate) {
        const QString installedVersion = installed->value(QStringLiteral("version")).toString();
        if (!entry->updateFor(host, platformKey, QVersionNumber::fromString(installedVersion))) {
            return tr("%1 %2 is installed, and the catalog has nothing newer").arg(entry->name, installedVersion);
        }
    }

    if (!_isAllowedSource(offer->package.url, _catalogIsLocal)) {
        qCWarning(PluginCatalogManagerLog) << "Refusing package URL" << offer->package.url << "for" << pluginId;
        return tr("Refusing to download %1 from %2: a package must come from an https:// URL")
            .arg(entry->name, offer->package.url);
    }

    // Outside the user plugins directory, so a package that fails a check below never
    // reaches it. QGCFileDownload leaves its file behind on a failed download, so
    // _finishInstall() deletes this directory whatever the outcome.
    _installDir = std::make_unique<QTemporaryDir>();
    if (!_installDir->isValid()) {
        const QString error = _installDir->errorString();
        _installDir.reset();
        return tr("Could not create a temporary directory: %1").arg(error);
    }

    QGCFileDownload* const download = new QGCFileDownload(this);
    download->setOutputPath(_installDir->filePath(QString::fromLatin1(kPackageFileName)));
    download->setExpectedHash(offer->package.sha256);
    (void) connect(download, &QGCFileDownload::downloadProgress, this, [this, download](qint64 bytesReceived, qint64) {
        if (download == _packageDownload) {
            _onPackageProgress(bytesReceived);
        }
    });
    // Queued: cancel() emits finished before it closes the file, and the handler deletes it.
    (void) connect(
        download, &QGCFileDownload::finished, this,
        [this, download](bool success, const QString&, const QString& errorMessage) {
            _onPackageFinished(download, success, errorMessage);
        },
        Qt::QueuedConnection);

    if (!download->start(offer->package.url)) {
        const QString error = download->errorString();
        download->disconnect(this);
        delete download;
        _installDir.reset();
        qCWarning(PluginCatalogManagerLog) << "Could not start the download of" << pluginId << "-" << error;
        return tr("Could not start the download: %1").arg(error);
    }

    qCDebug(PluginCatalogManagerLog) << (isUpdate ? "Updating" : "Installing") << pluginId
                                     << offer->version.manifest.version << "from" << offer->package.url;
    _packageDownload = download;
    _installOffer = *offer;
    _installIsUpdate = isUpdate;
    _installFailure.clear();
    _installingId = pluginId;
    _setInstallProgress(0.0);
    emit installingIdChanged();
    return QString();
}

void PluginCatalogManager::_setInstallProgress(qreal progress)
{
    if (!qFuzzyCompare(_installProgress, progress)) {
        _installProgress = progress;
        emit installProgressChanged();
    }
}

void PluginCatalogManager::_onPackageProgress(qint64 bytesReceived)
{
    const qint64 expectedSize = _installOffer.package.size;
    if (bytesReceived > expectedSize) {
        // Stop before an oversized download fills the disk; the hash check only runs at the end.
        if (_installFailure.isEmpty()) {
            _installFailure = tr("The download is larger than the %1 bytes the catalog lists").arg(expectedSize);
            _packageDownload->cancel();
        }
        return;
    }
    _setInstallProgress(static_cast<qreal>(bytesReceived) / static_cast<qreal>(expectedSize));
}

void PluginCatalogManager::_onPackageFinished(QGCFileDownload* download, bool success, const QString& errorMessage)
{
    if (download != _packageDownload) {
        return;
    }
    _packageDownload = nullptr;
    download->deleteLater();

    QString error;
    if (!_installFailure.isEmpty()) {
        error = _installFailure;
    } else if (!success) {
        // Only this flag says the download is good: on a hash mismatch or an HTTP error
        // QGCFileDownload still leaves bytes at the output path.
        error = tr("Download failed: %1").arg(errorMessage);
    } else {
        error = _installDownloaded(_installDir->filePath(QString::fromLatin1(kPackageFileName)));
    }
    _finishInstall(error.isEmpty(), error);
}

QString PluginCatalogManager::_installDownloaded(const QString& zipPath)
{
    const PluginCatalogPackage& package = _installOffer.package;
    const qint64 size = QFileInfo(zipPath).size();
    if (size != package.size) {
        return tr("The download is %1 bytes, but the catalog lists %2").arg(size).arg(package.size);
    }

    // The hash proves these are the bytes the index names. This proves the bytes are the
    // plugin the index names, so one entry cannot install or replace another plugin.
    QString manifestError;
    const PluginManifest manifest = PluginInstaller::readManifest(zipPath, &manifestError);
    if (manifest.id.isEmpty()) {
        return tr("The package is invalid: %1").arg(manifestError);
    }

    const PluginManifest& expected = _installOffer.version.manifest;
    if (manifest.id != expected.id) {
        return tr("The package declares id %1, but the catalog lists %2").arg(manifest.id, expected.id);
    }
    if (manifest.version != expected.version) {
        return tr("The package declares version %1, but the catalog lists %2")
            .arg(manifest.version.toString(), expected.version.toString());
    }
    if (manifest.tier != expected.tier) {
        return tr("The package declares tier %1, but the catalog lists %2")
            .arg(PluginManifest::tierToString(manifest.tier), PluginManifest::tierToString(expected.tier));
    }

    if (!_pluginManager) {
        return tr("Plugins are not available");
    }
    return _installIsUpdate ? _pluginManager->stagePluginUpdate(expected.id, zipPath)
                            : _pluginManager->installPlugin(zipPath);
}

void PluginCatalogManager::_finishInstall(bool success, const QString& errorString)
{
    if (_installDir && !_installDir->remove()) {
        qCWarning(PluginCatalogManagerLog) << "Could not delete the downloaded package at" << _installDir->path();
    }
    _installDir.reset();

    const QString pluginId = _installingId;
    const bool staged = success && _installIsUpdate;
    _installingId.clear();
    _installOffer = PluginCatalogOffer();
    _installIsUpdate = false;
    _installFailure.clear();

    if (success) {
        qCDebug(PluginCatalogManagerLog) << (staged ? "Staged update of" : "Installed") << pluginId;
    } else {
        qCWarning(PluginCatalogManagerLog) << "Install of" << pluginId << "failed -" << errorString;
    }

    _setInstallProgress(0.0);
    emit installingIdChanged();
    emit installFinished(pluginId, success, errorString, staged);
}
