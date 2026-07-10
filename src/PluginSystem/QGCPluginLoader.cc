/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCPluginLoader.h"
#include "QGCPlugin.h"
#include "QGCPluginInterface.h"
#include "QGCLoggingCategory.h"
#include "qgc_version.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QJsonObject>
#include <QtCore/QPluginLoader>
#include <QtCore/QStandardPaths>

QGC_LOGGING_CATEGORY(QGCPluginLoaderLog, "PluginSystem.QGCPluginLoader")

QGCPluginLoader::QGCPluginLoader(QObject* parent)
    : QObject(parent)
{
}

QGCPluginLoader::~QGCPluginLoader()
{
    // Plugins are owned by QGCApplication, don't delete here
}

QList<QGCPlugin*> QGCPluginLoader::loadedPlugins() const
{
    QList<QGCPlugin*> plugins;
    for (const PluginLoadInfo& info : _pluginInfos) {
        if (info.state == PluginState::Active) {
            plugins.append(info.plugin);
        }
    }
    return plugins;
}

QList<PluginLoadInfo> QGCPluginLoader::loadedPluginInfos() const
{
    QList<PluginLoadInfo> infos;
    for (const PluginLoadInfo& info : _pluginInfos) {
        if (info.state == PluginState::Active) {
            infos.append(info);
        }
    }
    return infos;
}

void QGCPluginLoader::loadPlugins(const QString& pluginDir)
{
    loadPlugins(QStringList() << pluginDir);
}

void QGCPluginLoader::loadPlugins(const QStringList& pluginDirs)
{
    qCDebug(QGCPluginLoaderLog) << "Loading plugins from" << pluginDirs.size() << "directories";

    for (const QString& dirPath : pluginDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) {
            qCDebug(QGCPluginLoaderLog) << "Plugin directory does not exist:" << dirPath;
            continue;
        }

        qCDebug(QGCPluginLoaderLog) << "Scanning plugin directory:" << dirPath;

        // Get platform-specific library extension
        QStringList filters;
#if defined(Q_OS_WIN)
        filters << "*.dll";
#elif defined(Q_OS_MACOS)
        filters << "*.dylib" << "*.bundle";
#else
        filters << "*.so";
#endif

        const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files);
        qCDebug(QGCPluginLoaderLog) << "Found" << entries.size() << "potential plugin files";

        for (const QFileInfo& fileInfo : entries) {
            _loadPlugin(fileInfo.absoluteFilePath());
        }
    }

    qCDebug(QGCPluginLoaderLog) << "Plugin loading complete." << loadedPluginInfos().size() << "plugins loaded";
}

PluginLoadInfo QGCPluginLoader::loadPlugin(const QString& filePath)
{
    return _loadPlugin(filePath);
}

PluginLoadInfo QGCPluginLoader::_loadPlugin(const QString& filePath)
{
    qCDebug(QGCPluginLoaderLog) << "Attempting to load plugin:" << filePath;

    PluginLoadInfo info = _inspect(filePath);
    if (info.state == PluginState::Discovered) {
        _activate(info);
    }

    _pluginInfos.append(info);

    if (info.state == PluginState::Active) {
        emit pluginLoaded(QFileInfo(filePath).fileName());
        qCDebug(QGCPluginLoaderLog) << "Successfully loaded plugin:" << filePath;
    } else {
        qCWarning(QGCPluginLoaderLog) << "Failed to load plugin:" << filePath << "-" << info.errorString;
        emit pluginLoadFailed(filePath, info.errorString);
    }

    return info;
}

PluginLoadInfo QGCPluginLoader::_inspect(const QString& filePath)
{
    PluginLoadInfo info;
    info.filePath = filePath;

    QPluginLoader loader(filePath);
    const QJsonObject envelope = loader.metaData();

    // An empty envelope means Qt couldn't read the library at all (wrong architecture,
    // not a Qt plugin, ...) — the legible reason is in errorString(), not the metadata.
    if (envelope.isEmpty()) {
        const QString loaderError = loader.errorString();
        info.state = PluginState::Failed;
        info.errorString = loaderError.isEmpty() ? QStringLiteral("no plugin metadata found") : loaderError;
        return info;
    }

    QString error;
    info.manifest = PluginManifest::fromMetaData(envelope, QStringLiteral(QGCPluginInterface_iid), &error);
    if (info.manifest.id.isEmpty()) {
        info.state = PluginState::Failed;
        info.errorString = error;
        return info;
    }

    QString reason;
    if (!info.manifest.validateForHost(hostInfo(), &reason)) {
        info.state = PluginState::Incompatible;
        info.errorString = reason;
        return info;
    }

    info.state = PluginState::Discovered;
    qCDebug(QGCPluginLoaderLog) << "Validated" << info.manifest.name
                                << "(" << PluginManifest::tierToString(info.manifest.tier)
                                << ", build" << info.manifest.hostBuildId << ") before load";
    return info;
}

void QGCPluginLoader::_activate(PluginLoadInfo& info)
{
    QPluginLoader loader(info.filePath);
    QObject* pluginObject = loader.instance();

    if (!pluginObject) {
        info.state = PluginState::Failed;
        info.errorString = loader.errorString();
        return;
    }

    auto* pluginInterface = qobject_cast<QGCPluginInterface*>(pluginObject);
    if (!pluginInterface) {
        info.state = PluginState::Failed;
        info.errorString = QStringLiteral("plugin does not implement QGCPluginInterface");
        loader.unload();
        return;
    }

    // Belt-and-braces runtime check; the manifest apiVersion gate is authoritative
    if (pluginInterface->pluginInterfaceVersion() != QGCPluginApiVersion) {
        info.state = PluginState::Failed;
        info.errorString = QStringLiteral("incompatible interface version: expected %1, got %2").arg(QGCPluginApiVersion).arg(pluginInterface->pluginInterfaceVersion());
        loader.unload();
        return;
    }

    // Note: We pass nullptr as parent because plugins are owned by QGCApplication
    // If we pass 'this' as parent, the plugins would be deleted when the loader is destroyed
    QGCPlugin* plugin = pluginInterface->createPlugin(nullptr);
    if (!plugin) {
        info.state = PluginState::Failed;
        info.errorString = QStringLiteral("failed to create plugin instance");
        loader.unload();
        return;
    }

    info.plugin = plugin;
    info.state = PluginState::Active;
}

HostInfo QGCPluginLoader::hostInfo()
{
    HostInfo host;

    // QGC_APP_VERSION_STR is a git describe string, e.g. "v5.0.3-1040-gabc1234"
    QString versionStr = QStringLiteral(QGC_APP_VERSION_STR);
    if (versionStr.startsWith(u'v')) {
        versionStr.remove(0, 1);
    }
    qsizetype suffixIndex = 0;
    host.version = QVersionNumber::fromString(versionStr, &suffixIndex);

    host.apiVersion = QGCPluginApiVersion;
    host.buildId = QStringLiteral(QGC_GIT_HASH);
    return host;
}

QStringList QGCPluginLoader::defaultPluginPaths()
{
    QStringList paths;

    // Application directory plugins folder
    QString appDirPath = QCoreApplication::applicationDirPath();
#if defined(Q_OS_MACOS)
    // On macOS, look inside the app bundle
    if (appDirPath.contains(".app/")) {
        QDir appDir(appDirPath);
        appDir.cdUp(); // Go from MacOS to Contents
        paths << appDir.absolutePath() + "/PlugIns";
    }
    paths << appDirPath + "/plugins";
#elif defined(Q_OS_WIN)
    paths << appDirPath + "/plugins";
#else
    paths << appDirPath + "/plugins";
    paths << appDirPath + "/../lib/qgroundcontrol/plugins"; // Linux: /usr/lib/qgroundcontrol/plugins
#endif

    // User data directory
#if defined(Q_OS_MACOS)
    QString userDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    paths << userDataPath + "/plugins";
#elif defined(Q_OS_WIN)
    QString userDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    paths << userDataPath + "/plugins";
#else
    QString userDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    paths << userDataPath + "/plugins";
#endif

    qCDebug(QGCPluginLoaderLog) << "Default plugin search paths:" << paths;
    return paths;
}
