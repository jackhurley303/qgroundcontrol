/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginInstaller.h"
#include "PluginManifest.h"
#include "QGCLoggingCategory.h"
#include "QGCPluginLoader.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>

#include "miniz.h"

#if defined(Q_OS_MACOS)
#include <sys/xattr.h>
#endif

QGC_LOGGING_CATEGORY(PluginInstallerLog, "PluginSystem.PluginInstaller")

namespace {

constexpr const char* kManifestFileName = "qgcplugin.json";

#if defined(Q_OS_MACOS)
constexpr const char* kQuarantineAttrName = "com.apple.quarantine";
#endif

// A zip entry name is safe to extract if, once its ".."/"." components are resolved,
// it stays inside the destination directory. Rejects absolute paths and any entry
// that would escape via "../" (zip-slip) — untrusted-content-in, so this is not optional.
bool isSafeEntryName(const QString& entryName)
{
    if (entryName.isEmpty() || QDir::isAbsolutePath(entryName)) {
        return false;
    }
    const QStringList parts = entryName.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int depth = 0;
    for (const QString& part : parts) {
        if (part == QStringLiteral("..")) {
            --depth;
            if (depth < 0) {
                return false;
            }
        } else if (part != QStringLiteral(".")) {
            ++depth;
        }
    }
    return true;
}

// Reads qgcplugin.json's bytes directly out of the zip archive without extracting
// anything else, so a malformed manifest is rejected before any file is written.
QByteArray readManifestBytesFromZip(mz_zip_archive* zip, QString* errorOut)
{
    const int index = mz_zip_reader_locate_file(zip, kManifestFileName, nullptr, 0);
    if (index < 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("archive does not contain %1 at its root").arg(QString::fromLatin1(kManifestFileName));
        }
        return {};
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(zip, static_cast<mz_uint>(index), &stat)) {
        if (errorOut) {
            *errorOut = QStringLiteral("could not stat %1 in archive").arg(QString::fromLatin1(kManifestFileName));
        }
        return {};
    }

    QByteArray bytes;
    bytes.resize(static_cast<qsizetype>(stat.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(zip, static_cast<mz_uint>(index), bytes.data(), static_cast<size_t>(bytes.size()), 0)) {
        if (errorOut) {
            *errorOut = QStringLiteral("could not read %1 from archive").arg(QString::fromLatin1(kManifestFileName));
        }
        return {};
    }
    return bytes;
}

// D12/04 §9: a package's sidecar manifest and bin/ binary are independently trusted
// (nothing re-validates one against the other at load time), so a package must not
// carry its own copy of the host's plugin ABI library or Qt itself — either would let
// installed content run a runtime the loader never checked. Entry names only; the
// archive isn't touched.
bool findBundledRuntimeEntry(mz_zip_archive* zip, QString* offendingNameOut)
{
    const mz_uint fileCount = mz_zip_reader_get_num_files(zip);
    for (mz_uint i = 0; i < fileCount; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(zip, i, &stat)) {
            continue;
        }

        const QString entryName = QString::fromUtf8(stat.m_filename);
        const QStringList parts = entryName.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            // Case-insensitive: this is a name-pattern heuristic, not a code check, so a
            // trivially re-cased entry (e.g. "qtcore.framework") must not slip past it.
            const bool isPluginApiDylib = part.startsWith(QStringLiteral("libQGCPluginAPI"), Qt::CaseInsensitive)
                && part.endsWith(QStringLiteral(".dylib"), Qt::CaseInsensitive);
            const bool isQtRuntime = part.startsWith(QStringLiteral("Qt"), Qt::CaseInsensitive)
                && (part.endsWith(QStringLiteral(".framework"), Qt::CaseInsensitive) || part.endsWith(QStringLiteral(".dylib"), Qt::CaseInsensitive));
            if (isPluginApiDylib || isQtRuntime) {
                if (offendingNameOut) {
                    *offendingNameOut = entryName;
                }
                return true;
            }
        }
    }
    return false;
}

bool extractAllTo(mz_zip_archive* zip, const QString& destDir, QString* errorOut)
{
    const mz_uint fileCount = mz_zip_reader_get_num_files(zip);

    for (mz_uint i = 0; i < fileCount; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(zip, i, &stat)) {
            if (errorOut) {
                *errorOut = QStringLiteral("could not stat archive entry %1").arg(i);
            }
            return false;
        }

        const QString entryName = QString::fromUtf8(stat.m_filename);
        if (!isSafeEntryName(entryName)) {
            if (errorOut) {
                *errorOut = QStringLiteral("archive entry '%1' has an unsafe path").arg(entryName);
            }
            return false;
        }

        const QString destPath = QDir(destDir).filePath(entryName);

        if (mz_zip_reader_is_file_a_directory(zip, i)) {
            if (!QDir().mkpath(destPath)) {
                if (errorOut) {
                    *errorOut = QStringLiteral("could not create directory '%1'").arg(destPath);
                }
                return false;
            }
            continue;
        }

        if (!QDir().mkpath(QFileInfo(destPath).absolutePath())) {
            if (errorOut) {
                *errorOut = QStringLiteral("could not create directory for '%1'").arg(destPath);
            }
            return false;
        }

        if (!mz_zip_reader_extract_to_file(zip, i, destPath.toUtf8().constData(), 0)) {
            if (errorOut) {
                *errorOut = QStringLiteral("could not extract '%1'").arg(entryName);
            }
            return false;
        }
    }

    return true;
}

} // namespace

QString PluginInstaller::userPluginsDir()
{
    const QStringList paths = QGCPluginLoader::defaultPluginPaths();
    return paths.isEmpty() ? QString() : paths.last();
}

PluginInstallResult PluginInstaller::installFromFile(const QString& zipPath)
{
    PluginInstallResult result;

    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0)) {
        result.errorString = QStringLiteral("could not open '%1' as a zip archive").arg(zipPath);
        qCWarning(PluginInstallerLog) << result.errorString;
        return result;
    }

    QString error;
    const QByteArray manifestBytes = readManifestBytesFromZip(&zip, &error);
    if (manifestBytes.isEmpty()) {
        mz_zip_reader_end(&zip);
        result.errorString = error;
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << error;
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        mz_zip_reader_end(&zip);
        result.errorString = QStringLiteral("malformed %1: %2").arg(QString::fromLatin1(kManifestFileName), parseError.errorString());
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    QString manifestError;
    const PluginManifest manifest = PluginManifest::fromJson(doc.object(), &manifestError);
    if (manifest.id.isEmpty()) {
        mz_zip_reader_end(&zip);
        result.errorString = QStringLiteral("invalid manifest: %1").arg(manifestError);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    if (manifest.tier == PluginManifest::Tier::Internal) {
        mz_zip_reader_end(&zip);
        result.errorString = QStringLiteral("tier internal cannot be packaged (dev-loop only); use tier sdk for a distributable binary plugin");
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    QString offendingEntry;
    if (findBundledRuntimeEntry(&zip, &offendingEntry)) {
        mz_zip_reader_end(&zip);
        result.errorString = QStringLiteral("archive bundles a runtime library ('%1'); plugins must link the host's QGCPluginAPI/Qt, not ship their own").arg(offendingEntry);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    const QString pluginsDir = userPluginsDir();
    if (pluginsDir.isEmpty() || !QDir().mkpath(pluginsDir)) {
        mz_zip_reader_end(&zip);
        result.errorString = QStringLiteral("could not create plugins directory");
        qCWarning(PluginInstallerLog) << result.errorString;
        return result;
    }

    const QString destDir = QDir(pluginsDir).filePath(manifest.id);

    // Collision on id: replace. The manifest we just validated is the new truth;
    // any prior install of this id (a previous version, or a stale/corrupt one) goes.
    if (QDir(destDir).exists()) {
        qCDebug(PluginInstallerLog) << "Replacing existing install of" << manifest.id << "at" << destDir;
        if (!QDir(destDir).removeRecursively()) {
            mz_zip_reader_end(&zip);
            result.errorString = QStringLiteral("could not remove existing install at '%1'").arg(destDir);
            qCWarning(PluginInstallerLog) << result.errorString;
            return result;
        }
    }

    if (!QDir().mkpath(destDir)) {
        mz_zip_reader_end(&zip);
        result.errorString = QStringLiteral("could not create '%1'").arg(destDir);
        qCWarning(PluginInstallerLog) << result.errorString;
        return result;
    }

    QString extractError;
    if (!extractAllTo(&zip, destDir, &extractError)) {
        mz_zip_reader_end(&zip);
        QDir(destDir).removeRecursively();
        result.errorString = extractError;
        qCWarning(PluginInstallerLog) << "Extraction of" << zipPath << "failed -" << extractError;
        return result;
    }

    mz_zip_reader_end(&zip);

    qCDebug(PluginInstallerLog) << "Installed" << manifest.id << "to" << destDir;
    result.success = true;
    result.pluginId = manifest.id;
    return result;
}

PluginInstallResult PluginInstaller::removePlugin(const QString& pluginId)
{
    PluginInstallResult result;
    result.pluginId = pluginId;

    const QString pluginsDir = userPluginsDir();
    if (pluginsDir.isEmpty()) {
        result.errorString = QStringLiteral("no user plugins directory");
        return result;
    }

    const QDir packageDir(QDir(pluginsDir).filePath(pluginId));
    if (!packageDir.exists()) {
        result.errorString = QStringLiteral("no installed package with id '%1'").arg(pluginId);
        qCWarning(PluginInstallerLog) << result.errorString;
        return result;
    }

    if (!QDir(packageDir).removeRecursively()) {
        result.errorString = QStringLiteral("could not remove '%1'").arg(packageDir.absolutePath());
        qCWarning(PluginInstallerLog) << result.errorString;
        return result;
    }

    qCDebug(PluginInstallerLog) << "Removed" << pluginId << "from" << packageDir.absolutePath();
    result.success = true;
    return result;
}

#if defined(Q_OS_MACOS)

bool PluginInstaller::isQuarantined(const QString& packageDir)
{
    // One representative file (the manifest, always present) rather than a full
    // recursive walk: this runs on every discovered package at every app startup,
    // and the OS applies quarantine uniformly to a tree from one archive-expand event.
    const QString manifestPath = QDir(packageDir).filePath(QString::fromLatin1(kManifestFileName));
    return getxattr(manifestPath.toUtf8().constData(), kQuarantineAttrName, nullptr, 0, 0, 0) >= 0;
}

bool PluginInstaller::stripQuarantine(const QString& packageDir)
{
    bool allStripped = true;
    QDirIterator it(packageDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QByteArray path = filePath.toUtf8();
        const ssize_t size = getxattr(path.constData(), kQuarantineAttrName, nullptr, 0, 0, 0);
        if (size < 0) {
            continue; // not quarantined
        }
        if (removexattr(path.constData(), kQuarantineAttrName, 0) != 0) {
            qCWarning(PluginInstallerLog) << "Could not strip quarantine from" << filePath;
            allStripped = false;
        }
    }
    return allStripped;
}

#endif
