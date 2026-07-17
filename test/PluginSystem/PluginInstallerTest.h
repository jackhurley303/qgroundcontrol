#pragma once

#include "BaseClasses/TempDirectoryTest.h"

#include <QtCore/QJsonObject>

/// TempDirectoryTest: builds real .qgcplugin zip fixtures on disk and installs them
/// into a temp-dir-backed user plugins directory (U3.2).
class PluginInstallerTest : public TempDirectoryTest
{
    Q_OBJECT

private slots:
    void init() override;

    void _installGoodZipSucceeds_test();
    void _installManifestlessZipRejected_test();
    void _installMalformedManifestRejected_test();
    void _installMissingFileRejected_test();
    void _installZipSlipEntryRejected_test();
    void _idCollisionReplaces_test();
    void _removeCleansDirectory_test();
    void _removeUnknownIdFails_test();
    void _installInternalTierZipRejectedPreExtraction_test();
    void _installBundledPluginApiDylibRejected_test();
    void _installBundledQtFrameworkRejected_test();

private:
    // Writes a zip at tempPath(zipRelPath) with the given entries (archive-relative
    // path -> content); returns the absolute zip path.
    QString _writeZip(const QString& zipRelPath, const QMap<QString, QByteArray>& entries);
    QJsonObject _validManifestJson(const QString& id, const QString& version = QStringLiteral("1.0.0"));
    QJsonObject _internalTierManifestJson(const QString& id);
};
