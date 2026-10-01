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

// Staging directories under the user plugins directory. The scan only discovers direct
// children that carry qgcplugin.json, so packages one level below these are never loaded.
constexpr const char* kPendingDirName = ".pending";
constexpr const char* kPreviousDirName = ".previous";

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

// A manifest id names a directory under the user plugins directory, so it must name
// exactly one child of it: no separators, and no leading '.', which also keeps it clear
// of "..", "." and the staging directories below.
bool isSafePackageId(const QString& pluginId)
{
    return !pluginId.isEmpty() && !pluginId.startsWith(QLatin1Char('.')) && !pluginId.contains(QLatin1Char('/')) &&
           !pluginId.contains(QLatin1Char('\\'));
}

// Every archive check installFromFile() and stageUpdate() share, run before anything is
// extracted. Returns a manifest with an empty id on rejection, with the reason in errorOut.
PluginManifest readPackageManifest(const QString& zipPath, QStringList* entryNamesOut, QString* errorOut)
{
    // Entry names for the whole archive, read before anything is extracted. The manifest
    // lookup and both entry-name checks below work off this one list.
    const QStringList entryNames = QGCCompression::listArchive(zipPath, QGCCompression::Format::ZIP);
    if (entryNames.isEmpty()) {
        *errorOut = QStringLiteral("could not open '%1' as a zip archive").arg(zipPath);
        qCWarning(PluginInstallerLog) << *errorOut;
        return PluginManifest();
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
        *errorOut = QStringLiteral("'%1' is not a zip archive").arg(zipPath);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }
    archiveFile.close();

    // Cheap string-only rules first, so a hostile package is rejected before any of its
    // data is decompressed.
    QString offendingEntry;
    if (findUnsafeEntry(entryNames, &offendingEntry)) {
        *errorOut = QStringLiteral("archive entry '%1' has an unsafe path").arg(offendingEntry);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }

    if (findBundledRuntimeEntry(entryNames, &offendingEntry)) {
        *errorOut = QStringLiteral(
                        "archive bundles a runtime library ('%1'); plugins must link the host's QGCPluginAPI/Qt, not "
                        "ship their own")
                        .arg(offendingEntry);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }

    // Exactly one manifest. Zip allows duplicate names, and the reader answers a by-name
    // lookup with the first match while extraction writes every entry in order — so two
    // qgcplugin.json entries would mean validating one manifest and installing another.
    const qsizetype manifestCount = entryNames.count(QLatin1String(kManifestFileName));
    if (manifestCount == 0) {
        *errorOut =
            QStringLiteral("archive does not contain %1 at its root").arg(QString::fromLatin1(kManifestFileName));
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }
    if (manifestCount > 1) {
        *errorOut = QStringLiteral("archive contains %1 copies of %2")
                        .arg(manifestCount)
                        .arg(QString::fromLatin1(kManifestFileName));
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }

    // Reads qgcplugin.json's bytes straight out of the archive without extracting
    // anything else, so a malformed manifest is rejected before any file is written.
    const QByteArray manifestBytes =
        QGCCompression::extractFileData(zipPath, QString::fromLatin1(kManifestFileName), QGCCompression::Format::ZIP);
    if (manifestBytes.isEmpty()) {
        *errorOut = QStringLiteral("could not read %1 from archive").arg(QString::fromLatin1(kManifestFileName));
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *errorOut =
            QStringLiteral("malformed %1: %2").arg(QString::fromLatin1(kManifestFileName), parseError.errorString());
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }

    QString manifestError;
    const PluginManifest manifest = PluginManifest::fromJson(doc.object(), &manifestError);
    if (manifest.id.isEmpty()) {
        *errorOut = QStringLiteral("invalid manifest: %1").arg(manifestError);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }

    // The id becomes a directory name under the user plugins directory. Without this,
    // ".." would point the install at that directory's parent and ".pending" at the
    // staged updates, and the replace step below deletes whatever is there.
    if (!isSafePackageId(manifest.id)) {
        *errorOut = QStringLiteral("manifest id '%1' cannot name a package directory").arg(manifest.id);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }

    if (manifest.tier == PluginManifest::Tier::HostPinned) {
        *errorOut = QStringLiteral(
            "tier internal cannot be packaged (dev-loop only); use tier sdk for a distributable binary plugin");
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << *errorOut;
        return PluginManifest();
    }

    *entryNamesOut = entryNames;
    return manifest;
}

// Extracts a package readPackageManifest() accepted into destDir, replacing whatever is
// there. On failure nothing is left at destDir.
bool extractPackage(const QString& zipPath, const QStringList& entryNames, const QString& destDir, QString* errorOut)
{
    // Collision on id: replace. The manifest just validated is the new truth; any prior
    // copy at this path (a previous version, or a stale/corrupt one) goes.
    if (QDir(destDir).exists()) {
        qCDebug(PluginInstallerLog) << "Replacing existing package at" << destDir;
        if (!QDir(destDir).removeRecursively()) {
            *errorOut = QStringLiteral("could not remove existing install at '%1'").arg(destDir);
            qCWarning(PluginInstallerLog) << *errorOut;
            return false;
        }
    }

    if (!QDir().mkpath(destDir)) {
        *errorOut = QStringLiteral("could not create '%1'").arg(destDir);
        qCWarning(PluginInstallerLog) << *errorOut;
        return false;
    }

    if (!QGCCompression::extractArchive(zipPath, destDir, QGCCompression::Format::ZIP, nullptr, kMaxPackageBytes)) {
        QDir(destDir).removeRecursively();
        *errorOut = QStringLiteral("could not extract '%1': %2").arg(zipPath, QGCCompression::lastErrorString());
        qCWarning(PluginInstallerLog) << "Extraction of" << zipPath << "failed -" << *errorOut;
        return false;
    }

    // Extraction reports success even when it silently skipped entries — an escaping
    // symlink and a name the listing pass read differently are both dropped with only a
    // warning — so confirm every listed entry actually landed. (It cannot catch an
    // archive whose headers stop early: the listing pass truncates at the same point, so
    // both agree on a short list. That needs QGCCompression itself to distinguish
    // ARCHIVE_EOF from ARCHIVE_FATAL.)
    QString offendingEntry;
    if (findMissingEntry(entryNames, destDir, &offendingEntry)) {
        QDir(destDir).removeRecursively();
        *errorOut = QStringLiteral("archive entry '%1' was not extracted").arg(offendingEntry);
        qCWarning(PluginInstallerLog) << "Extraction of" << zipPath << "failed -" << *errorOut;
        return false;
    }

    return true;
}

QString previousPackageDir(const QString& pluginsDir, const QString& pluginId)
{
    return QDir(pluginsDir).filePath(QStringLiteral("%1/%2").arg(QString::fromLatin1(kPreviousDirName), pluginId));
}

// A run killed between the two renames of an apply leaves the installed package under
// .previous and nothing in its place: put it back. A copy left there after a completed
// swap is only garbage.
void finishInterruptedUpdates(const QString& pluginsDir)
{
    const QDir previousRoot(QDir(pluginsDir).filePath(QString::fromLatin1(kPreviousDirName)));
    const QStringList ids = previousRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& pluginId : ids) {
        const QString previousDir = previousRoot.filePath(pluginId);
        const QString installedDir = QDir(pluginsDir).filePath(pluginId);
        if (QFileInfo::exists(installedDir)) {
            if (!QDir(previousDir).removeRecursively()) {
                qCWarning(PluginInstallerLog) << "Could not delete replaced package" << previousDir;
            }
        } else if (QDir().rename(previousDir, installedDir)) {
            qCWarning(PluginInstallerLog) << "Restored" << pluginId << "after an interrupted update";
        } else {
            qCWarning(PluginInstallerLog) << "Could not restore" << pluginId << "from" << previousDir;
        }
    }
}

PluginInstallResult applyPendingUpdate(const QString& pluginsDir, const QString& pluginId)
{
    PluginInstallResult result;
    result.pluginId = pluginId;

    const QString pendingDir = PluginInstaller::pendingUpdateDir(pluginId);
    const QString installedDir = QDir(pluginsDir).filePath(pluginId);
    const QString previousDir = previousPackageDir(pluginsDir, pluginId);

    const auto fail = [&](const QString& error) {
        result.errorString = error;
        qCWarning(PluginInstallerLog) << "Update of" << pluginId << "not applied -" << error;
        if (!PluginInstaller::discardPendingUpdate(pluginId)) {
            qCWarning(PluginInstallerLog) << "Could not discard the staged update at" << pendingDir;
        }
        return result;
    };

    if (pendingDir.isEmpty()) {
        return fail(QStringLiteral("'%1' is not a valid plugin id").arg(pluginId));
    }

    // An update replaces an installed package; it never installs one. Runs after
    // finishInterruptedUpdates(), so a package an interrupted swap set aside is back by now.
    if (!QFileInfo::exists(installedDir)) {
        return fail(QStringLiteral("'%1' is not installed, so there is nothing to update").arg(pluginId));
    }

    // The staged copy is re-inspected rather than trusted: it sat on disk since the
    // stage step, and only a package that would load may replace one that does.
    const PluginLoadInfo staged = QGCPluginLoader::inspectPackage(pendingDir);
    if (staged.state != PluginState::Discovered) {
        return fail(QStringLiteral("the staged update cannot load: %1").arg(staged.errorString));
    }
    if (staged.manifest.id != pluginId) {
        return fail(QStringLiteral("the staged update declares id '%1'").arg(staged.manifest.id));
    }

    if (!QDir().mkpath(QFileInfo(previousDir).absolutePath())) {
        return fail(QStringLiteral("could not create '%1'").arg(QFileInfo(previousDir).absolutePath()));
    }

    // Rename the installed package aside, then rename the staged one into its place. A
    // directory rename either happens whole or not at all, so the old package is always
    // complete at one of the two paths.
    if (!QDir().rename(installedDir, previousDir)) {
        return fail(QStringLiteral("could not move the installed package aside"));
    }

    if (!QDir().rename(pendingDir, installedDir)) {
        if (!QDir().rename(previousDir, installedDir)) {
            // finishInterruptedUpdates() retries the restore at the next start.
            return fail(QStringLiteral("could not move the update into place, and the previous version is at '%1'")
                            .arg(previousDir));
        }
        return fail(QStringLiteral("could not move the update into place"));
    }

    if (!QDir(previousDir).removeRecursively()) {
        qCWarning(PluginInstallerLog) << "Could not delete replaced package" << previousDir;
    }

    qCDebug(PluginInstallerLog) << "Applied staged update of" << pluginId << "to version" << staged.manifest.version;
    result.success = true;
    return result;
}

}  // namespace

PluginManifest PluginInstaller::readManifest(const QString& zipPath, QString* errorOut)
{
    QStringList entryNames;
    QString error;
    const PluginManifest manifest = readPackageManifest(zipPath, &entryNames, &error);
    if (manifest.id.isEmpty() && errorOut) {
        *errorOut = error;
    }
    return manifest;
}

QString PluginInstaller::userPluginsDir()
{
    const QStringList paths = QGCPluginLoader::defaultPluginPaths();
    return paths.isEmpty() ? QString() : paths.last();
}

QString PluginInstaller::pendingUpdateDir(const QString& pluginId)
{
    const QString pluginsDir = userPluginsDir();
    if (pluginsDir.isEmpty() || !isSafePackageId(pluginId)) {
        return QString();
    }
    return QDir(pluginsDir).filePath(QStringLiteral("%1/%2").arg(QString::fromLatin1(kPendingDirName), pluginId));
}

PluginInstallResult PluginInstaller::installFromFile(const QString& zipPath)
{
    PluginInstallResult result;

    QStringList entryNames;
    const PluginManifest manifest = readPackageManifest(zipPath, &entryNames, &result.errorString);
    if (manifest.id.isEmpty()) {
        return result;
    }

    const QString pluginsDir = userPluginsDir();
    if (pluginsDir.isEmpty() || !QDir().mkpath(pluginsDir)) {
        result.errorString = QStringLiteral("could not create plugins directory");
        qCWarning(PluginInstallerLog) << result.errorString;
        return result;
    }

    const QString destDir = QDir(pluginsDir).filePath(manifest.id);
    if (!extractPackage(zipPath, entryNames, destDir, &result.errorString)) {
        return result;
    }

    qCDebug(PluginInstallerLog) << "Installed" << manifest.id << "to" << destDir;
    result.success = true;
    result.pluginId = manifest.id;
    return result;
}

PluginInstallResult PluginInstaller::stageUpdate(const QString& pluginId, const QString& zipPath)
{
    PluginInstallResult result;
    result.pluginId = pluginId;

    QStringList entryNames;
    const PluginManifest manifest = readPackageManifest(zipPath, &entryNames, &result.errorString);
    if (manifest.id.isEmpty()) {
        return result;
    }

    // The caller's consent is for updating pluginId. A package declaring another id
    // would otherwise replace that plugin under this plugin's consent.
    if (manifest.id != pluginId) {
        result.errorString = QStringLiteral("package declares id '%1', not '%2'").arg(manifest.id, pluginId);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    const QString pendingDir = pendingUpdateDir(manifest.id);
    if (pendingDir.isEmpty()) {
        result.errorString = QStringLiteral("'%1' is not a valid plugin id").arg(manifest.id);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    // A fresh install activates at once through installFromFile(); only a package
    // already in place has a mapped binary to wait out.
    if (!QFileInfo::exists(QDir(userPluginsDir()).filePath(manifest.id))) {
        result.errorString = QStringLiteral("'%1' is not installed, so there is nothing to update").arg(manifest.id);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    if (!extractPackage(zipPath, entryNames, pendingDir, &result.errorString)) {
        return result;
    }

    // Refuse now what applyPendingUpdates() would refuse at the next start, while the
    // user is still looking at the result.
    const PluginLoadInfo staged = QGCPluginLoader::inspectPackage(pendingDir);
    if (staged.state != PluginState::Discovered) {
        QDir(pendingDir).removeRecursively();
        result.errorString = QStringLiteral("the update cannot load: %1").arg(staged.errorString);
        qCWarning(PluginInstallerLog) << "Rejecting" << zipPath << "-" << result.errorString;
        return result;
    }

    qCDebug(PluginInstallerLog) << "Staged" << manifest.id << manifest.version << "at" << pendingDir;
    result.success = true;
    return result;
}

QList<PluginInstallResult> PluginInstaller::applyPendingUpdates()
{
    QList<PluginInstallResult> results;

    const QString pluginsDir = userPluginsDir();
    if (pluginsDir.isEmpty()) {
        return results;
    }

    finishInterruptedUpdates(pluginsDir);

    const QDir pendingRoot(QDir(pluginsDir).filePath(QString::fromLatin1(kPendingDirName)));
    const QStringList ids = pendingRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& pluginId : ids) {
        results.append(applyPendingUpdate(pluginsDir, pluginId));
    }
    return results;
}

bool PluginInstaller::discardPendingUpdate(const QString& pluginId)
{
    const QString pendingDir = pendingUpdateDir(pluginId);
    return pendingDir.isEmpty() || QDir(pendingDir).removeRecursively();
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
