/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginTrustGate.h"
#include "PluginInstaller.h"
#include "PluginRecordStore.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace {

QString packageManifestPath(const PluginLoadInfo& record)
{
    return QDir(record.packageDir).filePath(QStringLiteral("qgcplugin.json"));
}

} // namespace

bool PluginTrustGate::isUserDirPlugin(const PluginLoadInfo& record)
{
    const QString userPluginsDir = PluginInstaller::userPluginsDir();
    if (userPluginsDir.isEmpty()) {
        return false;
    }
    const QString container = record.packageDir.isEmpty() ? record.filePath : record.packageDir;
    return !container.isEmpty()
        && QFileInfo(container).dir().absolutePath() == QDir(userPluginsDir).absolutePath();
}

QString PluginTrustGate::consentDigest(const PluginLoadInfo& record)
{
    QStringList files;
    if (!record.packageDir.isEmpty()) {
        files << packageManifestPath(record);
    }
    if (record.filePath != record.packageDir) {
        files << record.filePath;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& filePath : files) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
            return QString();
        }
    }
    return record.manifest.version.toString() + QLatin1Char(':') + QString::fromLatin1(hash.result().toHex());
}

void PluginTrustGate::applyTrustGate(PluginLoadInfo& record, PluginRecordStore& store)
{
    if (record.state != PluginState::Discovered) {
        return;
    }

    // Crash sentinel: the last run died inside this plugin's activation. Checked
    // first — it outranks NeedsApproval, because a plugin that crashed the host must
    // not become runnable by mere consent. Only an explicit re-enable
    // (setPluginEnabled) clears the marker.
    const QString crashedPluginId = store.crashedPluginId();
    if (!crashedPluginId.isEmpty() && record.manifest.id == crashedPluginId) {
        record.state = PluginState::Quarantined;
        record.errorString = QObject::tr("QGC crashed while loading this plugin last run — re-enable to retry");
        return;
    }

#if defined(Q_OS_MACOS)
    // Manually-dropped plugins (not extracted in-process by installFromFile(), 01 §1.4)
    // may carry com.apple.quarantine from however they arrived. Check the manifest and
    // the resolved binary (D15): manifest-only would miss a fresh quarantined dylib
    // swapped into an otherwise clean package, which Gatekeeper then kills cryptically
    // at dlopen. Applies in every search dir — quarantine means "downloaded", wherever
    // the file was dropped.
    bool quarantined = false;
    if (!record.packageDir.isEmpty()) {
        quarantined = PluginInstaller::isFileQuarantined(packageManifestPath(record));
    }
    if (!quarantined && record.filePath != record.packageDir) {
        quarantined = PluginInstaller::isFileQuarantined(record.filePath);
    }
    if (quarantined) {
        record.state = PluginState::NeedsApproval;
        record.errorString = QObject::tr("Downloaded plugin — approve to run");
        return;
    }
#endif

    // D10: bundle-dir plugins are trusted; a user-dir plugin runs only with recorded
    // consent, keyed to its content — a changed plugin must re-prompt.
    if (isUserDirPlugin(record)) {
        const QString approved = store.approvedPluginDigest(record.manifest.id);
        const QString digest = consentDigest(record);
        if (digest.isEmpty() || digest != approved) {
            record.state = PluginState::NeedsApproval;
            record.errorString = approved.isEmpty()
                ? QObject::tr("New plugin — approve to run")
                : QObject::tr("Plugin changed since approval — approve again to run");
        }
    }
}
