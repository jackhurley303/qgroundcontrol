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
#include "QGCCompression.h"
#include "QGCLoggingCategory.h"
#include "QGCPluginLoader.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>

#if defined(Q_OS_MACOS)
#include <sys/xattr.h>
#endif

QGC_LOGGING_CATEGORY(PluginInstallerLog, "PluginSystem.PluginInstaller")

namespace {

constexpr const char* kManifestFileName = "qgcplugin.json";

// Extraction ceiling for untrusted packages. A plugin is a binary plus QML and assets;
// anything past this is a decompression bomb, not a package. libarchive's own pre-check
// only compares the archive's *declared* sizes against free disk space, so a bomb sized
// just under it would otherwise fill the disk.
constexpr qint64 kMaxPackageBytes = 512LL * 1024 * 1024;

// Enough leading bytes for magic-number format detection.
constexpr qint64 kMagicBytesToRead = 512;

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

// The first entry whose name would escape the destination directory, if any.
// QGCCompression skips such an entry with a warning and extracts the rest; an
// untrusted package that contains one is rejected outright instead.
bool findUnsafeEntry(const QStringList& entryNames, QString* offendingNameOut)
{
    for (const QString& entryName : entryNames) {
        if (!isSafeEntryName(entryName)) {
            if (offendingNameOut) {
                *offendingNameOut = entryName;
            }
            return true;
        }
    }
    return false;
}

// D12/04 §9: a package's sidecar manifest and bin/ binary are independently trusted
// (nothing re-validates one against the other at load time), so a package must not
// carry its own copy of the host's plugin ABI library or Qt itself — either would let
// installed content run a runtime the loader never checked. Entry names only; the
// archive isn't touched.
bool findBundledRuntimeEntry(const QStringList& entryNames, QString* offendingNameOut)
{
    for (const QString& entryName : entryNames) {
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

// The first listed entry with nothing on disk to show for it, if any. Directory entries
// (trailing '/') are skipped — parents are created implicitly for nested files, so their
// absence is not evidence of a failed extraction. A dangling symlink counts as present:
// it was extracted, whatever its target resolves to.
bool findMissingEntry(const QStringList& entryNames, const QString& destDir, QString* missingNameOut)
{
    const QDir dir(destDir);
    for (const QString& entryName : entryNames) {
        if (entryName.endsWith(QLatin1Char('/'))) {
            continue;
        }
        const QFileInfo info(dir.filePath(entryName));
        if (!info.exists() && !info.isSymLink()) {
            if (missingNameOut) {
                *missingNameOut = entryName;
            }
            return true;
        }
    }
    return false;
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

    // Entry names for the whole archive, read before anything is extracted. The manifest
    // lookup and both entry-name checks below work off this one list.
    const QStringList entryNames = QGCCompression::listArchive(zipPath, QGCCompression::Format::ZIP);
    if (entryNames.isEmpty()) {
        result.errorString = QStringLiteral("could not open '%1' as a zip archive").arg(zipPath);
        qCWarning(PluginInstallerLog) << result.errorString;
        return result;
    }

    // The Format argument above only skips QGCCompression's own sniffing — the reader is
    // opened with every libarchive format enabled, so a tar/cpio/7z named .qgcplugin would
    // otherwise be extracted just as happily. Require a real zip: the other containers carry
    // entry types (hardlinks, device nodes) whose targets the name checks below cannot see,
    // and a hardlink's target is not confined to the destination directory.
    // Magic bytes rather than detectFormatFromFile(), which consults QMimeDatabase first
    // and can only answer ambiguously for a container whose entries are stored uncompressed.
    QFile archiveFile(zipPath);
    if (!archiveFile.open(QIODevice::ReadOnly) ||
        QGCCompression::detectFormatFromData(archiveFile.read(kMagicBytesToRead)) != QGCCompression::Format::ZIP) {
        result.errorString = QStringLiteral("'%1' is not a zip archive").arg(zipPath);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }
    archiveFile.close();

    // Cheap string-only rules first, so a hostile package is rejected before any of its
    // data is decompressed.
    QString offendingEntry;
    if (findUnsafeEntry(entryNames, &offendingEntry)) {
        result.errorString = QStringLiteral("archive entry '%1' has an unsafe path").arg(offendingEntry);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    if (findBundledRuntimeEntry(entryNames, &offendingEntry)) {
        result.errorString = QStringLiteral("archive bundles a runtime library ('%1'); plugins must link the host's QGCPluginAPI/Qt, not ship their own").arg(offendingEntry);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    // Exactly one manifest. Zip allows duplicate names, and the reader answers a by-name
    // lookup with the first match while extraction writes every entry in order — so two
    // qgcplugin.json entries would mean validating one manifest and installing another.
    const qsizetype manifestCount = entryNames.count(QLatin1String(kManifestFileName));
    if (manifestCount == 0) {
        result.errorString =
            QStringLiteral("archive does not contain %1 at its root").arg(QString::fromLatin1(kManifestFileName));
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }
    if (manifestCount > 1) {
        result.errorString = QStringLiteral("archive contains %1 copies of %2")
                                 .arg(manifestCount)
                                 .arg(QString::fromLatin1(kManifestFileName));
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    // Reads qgcplugin.json's bytes straight out of the archive without extracting
    // anything else, so a malformed manifest is rejected before any file is written.
    const QByteArray manifestBytes =
        QGCCompression::extractFileData(zipPath, QString::fromLatin1(kManifestFileName), QGCCompression::Format::ZIP);
    if (manifestBytes.isEmpty()) {
        result.errorString =
            QStringLiteral("could not read %1 from archive").arg(QString::fromLatin1(kManifestFileName));
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.errorString = QStringLiteral("malformed %1: %2").arg(QString::fromLatin1(kManifestFileName), parseError.errorString());
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    QString manifestError;
    const PluginManifest manifest = PluginManifest::fromJson(doc.object(), &manifestError);
    if (manifest.id.isEmpty()) {
        result.errorString = QStringLiteral("invalid manifest: %1").arg(manifestError);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    if (manifest.tier == PluginManifest::Tier::Internal) {
        result.errorString = QStringLiteral("tier internal cannot be packaged (dev-loop only); use tier sdk for a distributable binary plugin");
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    const QString pluginsDir = userPluginsDir();
    if (pluginsDir.isEmpty() || !QDir().mkpath(pluginsDir)) {
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
            result.errorString = QStringLiteral("could not remove existing install at '%1'").arg(destDir);
            qCWarning(PluginInstallerLog) << result.errorString;
            return result;
        }
    }

    if (!QDir().mkpath(destDir)) {
        result.errorString = QStringLiteral("could not create '%1'").arg(destDir);
        qCWarning(PluginInstallerLog) << result.errorString;
        return result;
    }

    if (!QGCCompression::extractArchive(zipPath, destDir, QGCCompression::Format::ZIP, nullptr, kMaxPackageBytes)) {
        QDir(destDir).removeRecursively();
        result.errorString =
            QStringLiteral("could not extract '%1': %2").arg(zipPath, QGCCompression::lastErrorString());
        qCWarning(PluginInstallerLog) << "Extraction of" << zipPath << "failed -" << result.errorString;
        return result;
    }

    // Extraction reports success even when it silently skipped entries — an escaping
    // symlink and a name the listing pass read differently are both dropped with only a
    // warning — so confirm every listed entry actually landed. (It cannot catch an
    // archive whose headers stop early: the listing pass truncates at the same point, so
    // both agree on a short list. That needs QGCCompression itself to distinguish
    // ARCHIVE_EOF from ARCHIVE_FATAL.)
    if (findMissingEntry(entryNames, destDir, &offendingEntry)) {
        QDir(destDir).removeRecursively();
        result.errorString = QStringLiteral("archive entry '%1' was not extracted").arg(offendingEntry);
        qCWarning(PluginInstallerLog) << "Extraction of" << zipPath << "failed -" << result.errorString;
        return result;
    }

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

bool PluginInstaller::isFileQuarantined(const QString& filePath)
{
    return getxattr(filePath.toUtf8().constData(), kQuarantineAttrName, nullptr, 0, 0, 0) >= 0;
}

namespace {

bool stripQuarantineFromFile(const QString& filePath)
{
    const QByteArray path = filePath.toUtf8();
    if (getxattr(path.constData(), kQuarantineAttrName, nullptr, 0, 0, 0) < 0) {
        return true; // not quarantined
    }
    if (removexattr(path.constData(), kQuarantineAttrName, 0) != 0) {
        qCWarning(PluginInstallerLog) << "Could not strip quarantine from" << filePath;
        return false;
    }
    return true;
}

} // namespace

bool PluginInstaller::stripQuarantine(const QString& path)
{
    if (QFileInfo(path).isFile()) {
        return stripQuarantineFromFile(path);
    }

    bool allStripped = true;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        allStripped &= stripQuarantineFromFile(it.next());
    }
    return allStripped;
}

#endif
