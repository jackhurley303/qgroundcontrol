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
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QPluginLoader>
#include <QtCore/QStandardPaths>

QGC_LOGGING_CATEGORY(QGCPluginLoaderLog, "PluginSystem.QGCPluginLoader")

namespace {

// Platform-specific plugin binary extension, shared by the dev-loop bare-file scan
// and package bin/ discovery.
QStringList pluginBinaryFilters()
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("*.dll")};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("*.dylib"), QStringLiteral("*.bundle")};
#else
    return {QStringLiteral("*.so")};
#endif
}

// The package format's documented per-platform bin/ subdirectory key (02 §6).
QString platformBinarySubdir()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows-x64");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos-universal");
#else
    return QStringLiteral("linux-x64");
#endif
}

// Finds a package's binary under bin/: the documented key subdirectory first
// (bin/macos-universal/ on macOS), falling back to any single binary under a
// platform-prefixed subdirectory (bin/macos-*/) — the fallback the plan documents
// for e.g. a single-arch bin/macos-x86_64/ layout.
QStringList findPackageBinaries(const QString& binDir)
{
    QDir bin(binDir);
    if (!bin.exists()) {
        return {};
    }

    const QStringList filters = pluginBinaryFilters();

    QDir primary(bin.filePath(platformBinarySubdir()));
    if (primary.exists()) {
        const QFileInfoList entries = primary.entryInfoList(filters, QDir::Files, QDir::Name);
        if (!entries.isEmpty()) {
            QStringList paths;
            for (const QFileInfo& fi : entries) {
                paths << fi.absoluteFilePath();
            }
            return paths;
        }
    }

    QStringList paths;
    const QString platformPrefix = platformBinarySubdir().section(QLatin1Char('-'), 0, 0);
    const QFileInfoList subdirs = bin.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& subdirInfo : subdirs) {
        if (!subdirInfo.fileName().startsWith(platformPrefix)) {
            continue;
        }
        const QDir subdir(subdirInfo.absoluteFilePath());
        const QFileInfoList entries = subdir.entryInfoList(filters, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : entries) {
            paths << fi.absoluteFilePath();
        }
    }
    return paths;
}

// Shared tail of inspect()/inspectPackage(): once a manifest is parsed (and any
// tier-specific checks the two shapes don't share have run), host-compatibility
// validation, contribution derivation, and Discovered-state logging are identical.
// info.packageDir is empty for a bare dev-loop dylib and set for a package (D8),
// which is also how the two call sites already tell their log message apart.

bool validateHostCompatibility(PluginLoadInfo& info)
{
    QString reason;
    if (!info.manifest.validateForHost(QGCPluginLoader::hostInfo(), &reason)) {
        info.state = PluginState::Incompatible;
        info.errorString = reason;
        return false;
    }
    return true;
}

bool deriveContributions(PluginLoadInfo& info)
{
    QString error;
    info.contributions = PluginContributions::fromManifest(info.manifest, info.packageDir, &error);
    if (!error.isEmpty()) {
        info.state = PluginState::Failed;
        info.errorString = error;
        return false;
    }
    return true;
}

void markDiscovered(PluginLoadInfo& info)
{
    info.state = PluginState::Discovered;
    if (info.packageDir.isEmpty()) {
        qCDebug(QGCPluginLoaderLog) << "Validated" << info.manifest.name
                                    << "(" << PluginManifest::tierToString(info.manifest.tier)
                                    << ", build" << info.manifest.hostBuildId << ") before load";
    } else {
        qCDebug(QGCPluginLoaderLog) << "Validated package" << info.manifest.name
                                    << "(" << PluginManifest::tierToString(info.manifest.tier)
                                    << ") at" << info.packageDir;
    }
}

} // namespace

QList<PluginLoadInfo> QGCPluginLoader::inspectDirectories(const QStringList& pluginDirs)
{
    qCDebug(QGCPluginLoaderLog) << "Inspecting plugins in" << pluginDirs.size() << "directories";

    QList<PluginLoadInfo> infos;

    for (const QString& dirPath : pluginDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) {
            qCDebug(QGCPluginLoaderLog) << "Plugin directory does not exist:" << dirPath;
            continue;
        }

        qCDebug(QGCPluginLoaderLog) << "Scanning plugin directory:" << dirPath;

        // Bare plugin libraries (dev-loop path). Sorted so duplicate-id resolution in
        // the manager is deterministic across runs.
        const QFileInfoList entries = dir.entryInfoList(pluginBinaryFilters(), QDir::Files, QDir::Name);
        qCDebug(QGCPluginLoaderLog) << "Found" << entries.size() << "potential plugin files";

        for (const QFileInfo& fileInfo : entries) {
            infos.append(inspect(fileInfo.absoluteFilePath()));
        }

        // Package directories: any child directory with qgcplugin.json at its root (D8)
        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& subdirInfo : subdirs) {
            const QString packageDir = subdirInfo.absoluteFilePath();
            if (QFile::exists(packageDir + QStringLiteral("/qgcplugin.json"))) {
                infos.append(inspectPackage(packageDir));
            }
        }
    }

    return infos;
}

PluginLoadInfo QGCPluginLoader::inspect(const QString& filePath)
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

    // Tier qml has no binary at all (D1) — a compiled file declaring it is a tier
    // mismatch, not a plugin the loader should silently half-load (activate() would
    // otherwise no-op it without ever running its code).
    if (info.manifest.tier == PluginManifest::Tier::Qml) {
        info.state = PluginState::Failed;
        info.errorString = QStringLiteral("tier qml has no binary and cannot be declared by a compiled plugin file; ship it as a package directory instead");
        return info;
    }

    if (!validateHostCompatibility(info)) {
        return info;
    }

    if (!deriveContributions(info)) {
        return info;
    }

    markDiscovered(info);
    return info;
}

PluginLoadInfo QGCPluginLoader::inspectPackage(const QString& packageDir)
{
    PluginLoadInfo info;
    info.packageDir = packageDir;
    info.filePath = packageDir;

    QFile manifestFile(packageDir + QStringLiteral("/qgcplugin.json"));
    if (!manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        info.state = PluginState::Failed;
        info.errorString = QStringLiteral("cannot read qgcplugin.json in package %1").arg(packageDir);
        return info;
    }
    const QByteArray manifestBytes = manifestFile.readAll();
    manifestFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        info.state = PluginState::Failed;
        info.errorString = QStringLiteral("malformed qgcplugin.json in package %1: %2").arg(packageDir, parseError.errorString());
        return info;
    }

    QString error;
    info.manifest = PluginManifest::fromJson(doc.object(), &error);
    if (info.manifest.id.isEmpty()) {
        info.state = PluginState::Failed;
        info.errorString = error;
        return info;
    }

    if (!validateHostCompatibility(info)) {
        return info;
    }

    // Tier internal is dev-loop only (hostBuildId-gated to a same-commit build, D7) —
    // packaging it would let a package's binary be swapped for another without the
    // loader ever re-checking it against the sidecar manifest that granted it trust.
    if (info.manifest.tier == PluginManifest::Tier::HostPinned) {
        info.state = PluginState::Failed;
        info.errorString = QStringLiteral("tier internal cannot be packaged (dev-loop only); use tier sdk for a distributable binary plugin");
        return info;
    }

    const QStringList binaries = findPackageBinaries(packageDir + QStringLiteral("/bin"));

    if (info.manifest.tier == PluginManifest::Tier::Qml) {
        // Tier A: no binary at all (D1) — a package that ships one has a layout/tier
        // mismatch, not a plugin the loader should silently half-load.
        if (!binaries.isEmpty()) {
            info.state = PluginState::Failed;
            info.errorString = QStringLiteral("qml-tier package must not ship a binary (found %1)").arg(binaries.first());
            return info;
        }
    } else {
        if (binaries.size() != 1) {
            info.state = PluginState::Failed;
            info.errorString = binaries.isEmpty()
                ? QStringLiteral("no %1 binary found under bin/%2 or bin/%3-*").arg(PluginManifest::tierToString(info.manifest.tier), platformBinarySubdir(), platformBinarySubdir().section(QLatin1Char('-'), 0, 0))
                : QStringLiteral("multiple candidate binaries found (%1); expected exactly one").arg(binaries.join(QStringLiteral(", ")));
            return info;
        }
        info.filePath = binaries.first();
    }

    if (!deriveContributions(info)) {
        return info;
    }

    if (info.manifest.tier == PluginManifest::Tier::Qml
        && (info.contributions.providesReplayExtension || info.contributions.controlsTelemetryLogging)) {
        info.state = PluginState::Failed;
        info.errorString = QStringLiteral("qml-tier package cannot declare 'replay' or 'telemetryLogging' (no binary to implement them)");
        return info;
    }

    markDiscovered(info);
    return info;
}

void QGCPluginLoader::activate(PluginLoadInfo& info)
{
    if (info.manifest.tier == PluginManifest::Tier::Qml) {
        // No binary to instantiate: contributions already came from the manifest alone.
        info.state = PluginState::Active;
        return;
    }

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
    host.qmlApiVersion = QGCPluginQmlApiLevel;
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
