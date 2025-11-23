/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCPluginLoader.h"
#include "QGCCorePlugin.h"
#include "QGCCorePluginInterface.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QPluginLoader>
#include <QtCore/QStandardPaths>

QGC_LOGGING_CATEGORY(QGCPluginLoaderLog, "qgc.api.pluginloader")

QGCPluginLoader::QGCPluginLoader(QObject* parent)
    : QObject(parent)
{
}

QGCPluginLoader::~QGCPluginLoader()
{
    // Plugins are owned by QGCApplication, don't delete here
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
            const QString filePath = fileInfo.absoluteFilePath();
            qCDebug(QGCPluginLoaderLog) << "Attempting to load plugin:" << filePath;

            QGCCorePlugin* plugin = _loadPlugin(filePath);
            if (plugin) {
                _loadedPlugins.append(plugin);
                emit pluginLoaded(fileInfo.fileName());
                qCDebug(QGCPluginLoaderLog) << "Successfully loaded plugin:" << filePath;
            }
        }
    }

    qCDebug(QGCPluginLoaderLog) << "Plugin loading complete." << _loadedPlugins.size() << "plugins loaded";
}

QGCCorePlugin* QGCPluginLoader::_loadPlugin(const QString& filePath)
{
    QPluginLoader loader(filePath);
    QObject* pluginObject = loader.instance();

    if (!pluginObject) {
        const QString errorString = loader.errorString();
        qCWarning(QGCPluginLoaderLog) << "Failed to load plugin:" << filePath << "-" << errorString;
        emit pluginLoadFailed(filePath, errorString);
        return nullptr;
    }

    // Check if plugin implements our interface
    auto* pluginInterface = qobject_cast<QGCCorePluginInterface*>(pluginObject);
    if (!pluginInterface) {
        qCWarning(QGCPluginLoaderLog) << "Plugin does not implement QGCCorePluginInterface:" << filePath;
        emit pluginLoadFailed(filePath, "Plugin does not implement QGCCorePluginInterface");
        loader.unload();
        return nullptr;
    }

    // Check interface version
    if (pluginInterface->pluginInterfaceVersion() != 1) {
        qCWarning(QGCPluginLoaderLog) << "Plugin has incompatible interface version:" << filePath
                                      << "- Expected 1, got" << pluginInterface->pluginInterfaceVersion();
        emit pluginLoadFailed(filePath, QString("Incompatible interface version: %1").arg(pluginInterface->pluginInterfaceVersion()));
        loader.unload();
        return nullptr;
    }

    // Create plugin instance
    QGCCorePlugin* plugin = pluginInterface->createPlugin(this);
    if (!plugin) {
        qCWarning(QGCPluginLoaderLog) << "Plugin failed to create instance:" << filePath;
        emit pluginLoadFailed(filePath, "Failed to create plugin instance");
        loader.unload();
        return nullptr;
    }

    // Validate plugin
    if (!_validatePlugin(plugin)) {
        qCWarning(QGCPluginLoaderLog) << "Plugin validation failed:" << filePath;
        emit pluginLoadFailed(filePath, "Plugin validation failed");
        delete plugin;
        loader.unload();
        return nullptr;
    }

    return plugin;
}

bool QGCPluginLoader::_validatePlugin(QGCCorePlugin* plugin)
{
    if (!plugin) {
        return false;
    }

    // Basic validation - plugin must be valid QObject
    if (plugin->metaObject() == nullptr) {
        qCWarning(QGCPluginLoaderLog) << "Plugin has invalid metaobject";
        return false;
    }

    // Additional validation can be added here
    // - Check minimum QGC version requirements
    // - Validate plugin metadata
    // - Check dependencies

    return true;
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
