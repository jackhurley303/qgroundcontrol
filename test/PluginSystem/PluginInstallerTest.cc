#include "PluginInstallerTest.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QStandardPaths>

#include "PluginInstaller.h"
#include "PluginManifest.h"
#include "QGCPluginLoader.h"

#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

namespace {

// CRC-32 (IEEE 802.3, reflected) over an entry's bytes — the zip reader rejects an
// entry whose stored checksum doesn't match, so this has to be the real thing.
quint32 crc32Of(const QByteArray& data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (const char byte : data) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

void appendU16(QByteArray& out, quint16 value)
{
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
}

void appendU32(QByteArray& out, quint32 value)
{
    appendU16(out, static_cast<quint16>(value & 0xFFFF));
    appendU16(out, static_cast<quint16>((value >> 16) & 0xFFFF));
}

}  // namespace

void PluginInstallerTest::init()
{
    TempDirectoryTest::init();

    // Redirects QStandardPaths::AppDataLocation (and therefore
    // PluginInstaller::userPluginsDir()) to a Qt-managed, test-scoped location instead
    // of the developer's real application-support directory.
    QStandardPaths::setTestModeEnabled(true);
    QDir(PluginInstaller::userPluginsDir()).removeRecursively();
}

QJsonObject PluginInstallerTest::_validManifestJson(const QString& id, const QString& version)
{
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("name")] = QStringLiteral("Test Package");
    json[QStringLiteral("version")] = version;
    json[QStringLiteral("vendor")] = QStringLiteral("Test Org");
    json[QStringLiteral("description")] = QStringLiteral("Installer fixture");
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    hostVersion[QStringLiteral("max")] = QString();
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("contributes")] = QJsonObject();
    return json;
}

QJsonObject PluginInstallerTest::_internalTierManifestJson(const QString& id)
{
    QJsonObject json = _validManifestJson(id);
    json[QStringLiteral("tier")] = QStringLiteral("internal");
    json[QStringLiteral("apiVersion")] = 1;
    json[QStringLiteral("hostBuildId")] = QStringLiteral("some-other-build");
    return json;
}

QString PluginInstallerTest::_writeZip(const QString& zipRelPath, const QMap<QString, QByteArray>& entries)
{
    QList<QPair<QString, QByteArray>> ordered;
    ordered.reserve(entries.size());
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        ordered.append({it.key(), it.value()});
    }
    return _writeZipOrdered(zipRelPath, ordered);
}

QByteArray storedZipArchive(const QList<QPair<QString, QByteArray>>& entries)
{
    // Minimal zip writer: STORED (uncompressed) entries, one local file header each,
    // then the central directory and the end-of-central-directory record. QGC only ever
    // reads archives, so nothing in production can write one — and a few headers here
    // are cheaper than carrying a compression library for the test's sake alone.
    constexpr quint16 kVersion = 20;  // 2.0, host 0 (MS-DOS): external attrs are DOS attrs
    constexpr quint16 kMethodStored = 0;
    constexpr quint16 kDosTime = 0;
    constexpr quint16 kDosDate = 0x0021;  // 1980-01-01, the earliest representable DOS date
    constexpr quint32 kDosDirectoryAttr = 0x10;

    QByteArray localHeaders;
    QByteArray centralDirectory;
    quint16 entryCount = 0;

    for (const auto& [entryName, content] : entries) {
        const QByteArray name = entryName.toUtf8();
        const quint32 crc = crc32Of(content);
        const quint32 size = static_cast<quint32>(content.size());
        const quint32 localOffset = static_cast<quint32>(localHeaders.size());
        // A trailing '/' is what marks a directory entry; the DOS attribute bit says so
        // a second time, for readers that trust attributes over the name.
        const quint32 externalAttrs = entryName.endsWith(QLatin1Char('/')) ? kDosDirectoryAttr : 0;

        appendU32(localHeaders, 0x04034b50);
        appendU16(localHeaders, kVersion);
        appendU16(localHeaders, 0);  // general purpose bit flags
        appendU16(localHeaders, kMethodStored);
        appendU16(localHeaders, kDosTime);
        appendU16(localHeaders, kDosDate);
        appendU32(localHeaders, crc);
        appendU32(localHeaders, size);  // compressed size == uncompressed size when stored
        appendU32(localHeaders, size);
        appendU16(localHeaders, static_cast<quint16>(name.size()));
        appendU16(localHeaders, 0);  // extra field length
        localHeaders.append(name);
        localHeaders.append(content);

        appendU32(centralDirectory, 0x02014b50);
        appendU16(centralDirectory, kVersion);  // version made by
        appendU16(centralDirectory, kVersion);  // version needed to extract
        appendU16(centralDirectory, 0);
        appendU16(centralDirectory, kMethodStored);
        appendU16(centralDirectory, kDosTime);
        appendU16(centralDirectory, kDosDate);
        appendU32(centralDirectory, crc);
        appendU32(centralDirectory, size);
        appendU32(centralDirectory, size);
        appendU16(centralDirectory, static_cast<quint16>(name.size()));
        appendU16(centralDirectory, 0);  // extra field length
        appendU16(centralDirectory, 0);  // file comment length
        appendU16(centralDirectory, 0);  // disk number where the file starts
        appendU16(centralDirectory, 0);  // internal file attributes
        appendU32(centralDirectory, externalAttrs);
        appendU32(centralDirectory, localOffset);
        centralDirectory.append(name);

        ++entryCount;
    }

    QByteArray archive = localHeaders;
    const quint32 centralDirectoryOffset = static_cast<quint32>(archive.size());
    archive.append(centralDirectory);

    appendU32(archive, 0x06054b50);
    appendU16(archive, 0);           // number of this disk
    appendU16(archive, 0);           // disk where the central directory starts
    appendU16(archive, entryCount);  // central directory entries on this disk
    appendU16(archive, entryCount);  // central directory entries in total
    appendU32(archive, static_cast<quint32>(centralDirectory.size()));
    appendU32(archive, centralDirectoryOffset);
    appendU16(archive, 0);  // archive comment length

    return archive;
}

QString PluginInstallerTest::_writeZipOrdered(const QString& zipRelPath,
                                              const QList<QPair<QString, QByteArray>>& entries)
{
    const QByteArray archive = storedZipArchive(entries);

    const QString zipPath = tempPath(zipRelPath);
    QFile file(zipPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    if (file.write(archive) != archive.size()) {
        return QString();
    }
    file.close();

    return zipPath;
}

QString PluginInstallerTest::_writeUstarTar(const QString& tarRelPath, const QString& entryName,
                                            const QByteArray& content)
{
    // Minimal single-entry POSIX ustar archive: one 512-byte header, the content padded to
    // a 512-byte boundary, then two zero blocks to mark the end.
    constexpr int kBlockSize = 512;
    QByteArray header(kBlockSize, '\0');

    const QByteArray name = entryName.toUtf8();
    header.replace(0, name.size(), name);                      // name
    header.replace(100, 8, QByteArray("0000644\0", 8));        // mode
    header.replace(108, 8, QByteArray("0000000\0", 8));        // uid
    header.replace(116, 8, QByteArray("0000000\0", 8));        // gid
    const QByteArray sizeField = QByteArray::number(content.size(), 8).rightJustified(11, '0');
    header.replace(124, 11, sizeField);                        // size (octal)
    header.replace(136, 12, QByteArray("00000000000\0", 12));  // mtime
    header[156] = '0';                                         // typeflag: regular
    header.replace(257, 6, QByteArray("ustar\0", 6));          // magic
    header.replace(263, 2, QByteArray("00", 2));               // version

    // Checksum is computed with the checksum field itself read as spaces.
    header.replace(148, 8, QByteArray(8, ' '));
    quint32 checksum = 0;
    for (const char byte : header) {
        checksum += static_cast<quint8>(byte);
    }
    header.replace(148, 7, QByteArray::number(checksum, 8).rightJustified(6, '0') + QByteArray("\0", 1));
    header[155] = ' ';

    QByteArray archive = header;
    archive.append(content);
    if (const int remainder = archive.size() % kBlockSize; remainder != 0) {
        archive.append(QByteArray(kBlockSize - remainder, '\0'));
    }
    archive.append(QByteArray(2 * kBlockSize, '\0'));

    const QString tarPath = tempPath(tarRelPath);
    QFile file(tarPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    if (file.write(archive) != archive.size()) {
        return QString();
    }
    file.close();

    return tarPath;
}

void PluginInstallerTest::_installGoodZipSucceeds_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.goodpkg"))).toJson();
    entries[QStringLiteral("qml/View.qml")] = QByteArrayLiteral("import QtQuick\nItem {}\n");

    const QString zipPath = _writeZip(QStringLiteral("good.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    QVERIFY2(result.success, qPrintable(result.errorString));
    QCOMPARE(result.pluginId, QStringLiteral("org.test.goodpkg"));

    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath("org.test.goodpkg");
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/qgcplugin.json")));
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/qml/View.qml")));
}

void PluginInstallerTest::_installManifestlessZipRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qml/View.qml")] = QByteArrayLiteral("import QtQuick\nItem {}\n");

    const QString zipPath = _writeZip(QStringLiteral("nomanifest.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("archive does not contain qgcplugin.json at its root"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("qgcplugin.json")), qPrintable(result.errorString));

    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.goodpkg")).exists());
}

void PluginInstallerTest::_installMalformedManifestRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QByteArrayLiteral("{ this is not valid json");

    const QString zipPath = _writeZip(QStringLiteral("malformed.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("malformed qgcplugin.json"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("malformed")), qPrintable(result.errorString));
}

void PluginInstallerTest::_installMissingFileRejected_test()
{
    expectLogMessage("Utilities.QGCCompression", QtWarningMsg, QRegularExpression("File does not exist"));
    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("as a zip archive"));
    const PluginInstallResult result = PluginInstaller::installFromFile(tempPath(QStringLiteral("nonexistent.qgcplugin")));
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY(!result.errorString.isEmpty());
}

void PluginInstallerTest::_installCorruptZipRejected_test()
{
    // Exists, but is not an archive: the failure has to come from the reader, not from
    // a missing-file check, and it must not leave a half-built package behind.
    const QString zipPath = tempPath(QStringLiteral("corrupt.qgcplugin"));
    QFile corrupt(zipPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    corrupt.write("not an archive at all");
    corrupt.close();

    expectLogMessage("Utilities.QGClibarchive", QtWarningMsg, QRegularExpression("Unrecognized archive format"));
    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("as a zip archive"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("zip archive")), qPrintable(result.errorString));
}

void PluginInstallerTest::_installDuplicateManifestRejected_test()
{
    // Zip allows a name to repeat. A by-name lookup answers with the first match while
    // extraction writes every entry in order, so accepting this would mean validating one
    // manifest and installing another.
    QList<QPair<QString, QByteArray>> entries;
    entries.append({QStringLiteral("qgcplugin.json"),
                    QJsonDocument(_validManifestJson(QStringLiteral("org.test.benign"))).toJson()});
    entries.append({QStringLiteral("qgcplugin.json"),
                    QJsonDocument(_validManifestJson(QStringLiteral("org.test.smuggled"))).toJson()});

    const QString zipPath = _writeZipOrdered(QStringLiteral("dupmanifest.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("copies of qgcplugin.json"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);

    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.benign")).exists());
    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.smuggled")).exists());
}

void PluginInstallerTest::_installNonZipContainerRejected_test()
{
    // A .qgcplugin that is really a tar. libarchive reads every format it was built with,
    // so without an explicit format check this would extract — and tar carries entry types
    // (hardlinks, device nodes) that the entry-name checks cannot see.
    const QString tarPath =
        _writeUstarTar(QStringLiteral("actually.qgcplugin"), QStringLiteral("qgcplugin.json"),
                       QJsonDocument(_validManifestJson(QStringLiteral("org.test.tarpkg"))).toJson());
    QVERIFY(!tarPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("is not a zip archive"));
    const PluginInstallResult result = PluginInstaller::installFromFile(tarPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("not a zip")), qPrintable(result.errorString));

    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.tarpkg")).exists());
}

void PluginInstallerTest::_installNestedEntriesExtracted_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] =
        QJsonDocument(_validManifestJson(QStringLiteral("org.test.nested"))).toJson();
    entries[QStringLiteral("resources/")] = QByteArray();
    entries[QStringLiteral("resources/icons/plugin.svg")] = QByteArrayLiteral("<svg/>");
    entries[QStringLiteral("qml/pages/Deeply/Nested/View.qml")] = QByteArrayLiteral("import QtQuick\nItem {}\n");

    const QString zipPath = _writeZip(QStringLiteral("nested.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    QVERIFY2(result.success, qPrintable(result.errorString));

    // Parent directories are created for entries that have no directory entry of their
    // own, and content survives the round trip intact.
    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath("org.test.nested");
    QVERIFY(QDir(installedDir + QStringLiteral("/resources/icons")).exists());
    QVERIFY(QDir(installedDir + QStringLiteral("/qml/pages/Deeply/Nested")).exists());

    QFile nested(installedDir + QStringLiteral("/qml/pages/Deeply/Nested/View.qml"));
    QVERIFY(nested.open(QIODevice::ReadOnly));
    QCOMPARE(nested.readAll(), QByteArrayLiteral("import QtQuick\nItem {}\n"));
}

void PluginInstallerTest::_installAbsolutePathEntryRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] =
        QJsonDocument(_validManifestJson(QStringLiteral("org.test.abspkg"))).toJson();
    entries[QStringLiteral("/tmp/qgc_abs_escape.txt")] = QByteArrayLiteral("payload");

    const QString zipPath = _writeZip(QStringLiteral("abs.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("has an unsafe path"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("unsafe")), qPrintable(result.errorString));

    QVERIFY(!QFile::exists(QStringLiteral("/tmp/qgc_abs_escape.txt")));
    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.abspkg")).exists());
}

void PluginInstallerTest::_installZipSlipEntryRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.slippkg"))).toJson();
    entries[QStringLiteral("../../escaped.txt")] = QByteArrayLiteral("payload");

    const QString zipPath = _writeZip(QStringLiteral("slip.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("has an unsafe path"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("unsafe")), qPrintable(result.errorString));

    // The escape attempt must not have landed anywhere outside the plugins dir either.
    QVERIFY(!QFile::exists(tempPath(QStringLiteral("../escaped.txt"))));
}

void PluginInstallerTest::_idCollisionReplaces_test()
{
    QMap<QString, QByteArray> v1Entries;
    v1Entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.collide"), QStringLiteral("1.0.0"))).toJson();
    v1Entries[QStringLiteral("old-only.txt")] = QByteArrayLiteral("v1");
    const QString v1Zip = _writeZip(QStringLiteral("v1.qgcplugin"), v1Entries);
    QVERIFY(!v1Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);

    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath("org.test.collide");
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/old-only.txt")));

    QMap<QString, QByteArray> v2Entries;
    v2Entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.collide"), QStringLiteral("2.0.0"))).toJson();
    const QString v2Zip = _writeZip(QStringLiteral("v2.qgcplugin"), v2Entries);
    QVERIFY(!v2Zip.isEmpty());

    const PluginInstallResult result = PluginInstaller::installFromFile(v2Zip);
    QVERIFY2(result.success, qPrintable(result.errorString));

    // v1's file is gone (full replace, not a merge) and the manifest is v2's.
    QVERIFY(!QFile::exists(installedDir + QStringLiteral("/old-only.txt")));
    QFile manifestFile(installedDir + QStringLiteral("/qgcplugin.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject onDisk = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QCOMPARE(onDisk[QStringLiteral("version")].toString(), QStringLiteral("2.0.0"));
}

void PluginInstallerTest::_removeCleansDirectory_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.removeme"))).toJson();
    const QString zipPath = _writeZip(QStringLiteral("removeme.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(zipPath).success);

    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath("org.test.removeme");
    QVERIFY(QDir(installedDir).exists());

    const PluginInstallResult result = PluginInstaller::removePlugin(QStringLiteral("org.test.removeme"));
    QVERIFY2(result.success, qPrintable(result.errorString));
    QVERIFY(!QDir(installedDir).exists());
}

void PluginInstallerTest::_removeUnknownIdFails_test()
{
    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("no installed package with id"));
    const PluginInstallResult result = PluginInstaller::removePlugin(QStringLiteral("org.test.neverinstalled"));
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY(!result.errorString.isEmpty());
}

void PluginInstallerTest::_installInternalTierZipRejectedPreExtraction_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_internalTierManifestJson(QStringLiteral("org.test.internalpkg"))).toJson();

    const QString zipPath = _writeZip(QStringLiteral("internal.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("tier internal cannot be packaged"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("internal")), qPrintable(result.errorString));

    // Rejected before extraction: nothing was written under the plugins dir.
    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.internalpkg")).exists());
}

void PluginInstallerTest::_installBundledPluginApiDylibRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.bundledapi"))).toJson();
    entries[QStringLiteral("bin/macos-universal/libQGCPluginAPI.2.dylib")] = QByteArrayLiteral("not a real dylib");

    const QString zipPath = _writeZip(QStringLiteral("bundledapi.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("bundles a runtime library"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("bundles a runtime")), qPrintable(result.errorString));

    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.bundledapi")).exists());
}

void PluginInstallerTest::_installBundledQtFrameworkRejected_test()
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(_validManifestJson(QStringLiteral("org.test.bundledqt"))).toJson();
    entries[QStringLiteral("Frameworks/QtCore.framework/Versions/A/QtCore")] = QByteArrayLiteral("not a real framework");

    const QString zipPath = _writeZip(QStringLiteral("bundledqt.qgcplugin"), entries);
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("bundles a runtime library"));
    const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QVERIFY2(result.errorString.contains(QStringLiteral("bundles a runtime")), qPrintable(result.errorString));

    QVERIFY(!QDir(QDir(PluginInstaller::userPluginsDir()).filePath("org.test.bundledqt")).exists());
}

QString PluginInstallerTest::_writePackageZip(const QString& zipRelPath, const QJsonObject& manifestJson,
                                              const QString& markerName)
{
    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qgcplugin.json")] = QJsonDocument(manifestJson).toJson();
    entries[markerName] = markerName.toUtf8();
    return _writeZip(zipRelPath, entries);
}

QString PluginInstallerTest::_manifestVersionAt(const QString& packageDir)
{
    QFile manifestFile(QDir(packageDir).filePath(QStringLiteral("qgcplugin.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QJsonDocument::fromJson(manifestFile.readAll()).object()[QStringLiteral("version")].toString();
}

void PluginInstallerTest::_stageThenApplyReplacesPackage_test()
{
    const QString id = QStringLiteral("org.test.staged");
    const QString v1Zip =
        _writePackageZip(QStringLiteral("staged-v1.qgcplugin"), _validManifestJson(id, QStringLiteral("1.0.0")),
                         QStringLiteral("v1-only.txt"));
    const QString v2Zip =
        _writePackageZip(QStringLiteral("staged-v2.qgcplugin"), _validManifestJson(id, QStringLiteral("2.0.0")),
                         QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);

    const PluginInstallResult staged = PluginInstaller::stageUpdate(id, v2Zip);
    QVERIFY2(staged.success, qPrintable(staged.errorString));
    QCOMPARE(staged.pluginId, id);

    // Staging leaves the installed package alone until the next start
    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath(id);
    const QString pendingDir = PluginInstaller::pendingUpdateDir(id);
    QCOMPARE(_manifestVersionAt(installedDir), QStringLiteral("1.0.0"));
    QCOMPARE(_manifestVersionAt(pendingDir), QStringLiteral("2.0.0"));

    const QList<PluginInstallResult> results = PluginInstaller::applyPendingUpdates();
    QCOMPARE(results.size(), 1);
    QVERIFY2(results.first().success, qPrintable(results.first().errorString));
    QCOMPARE(results.first().pluginId, id);

    // A whole replace: v1's own file is gone, nothing is left staged or set aside
    QCOMPARE(_manifestVersionAt(installedDir), QStringLiteral("2.0.0"));
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/v2-only.txt")));
    QVERIFY(!QFile::exists(installedDir + QStringLiteral("/v1-only.txt")));
    QVERIFY(!QFileInfo::exists(pendingDir));
    QVERIFY(!QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(QStringLiteral(".previous/") + id)));

    // Nothing left to apply on the start after that
    QVERIFY(PluginInstaller::applyPendingUpdates().isEmpty());
}

void PluginInstallerTest::_stageUninstalledIdRefused_test()
{
    const QString id = QStringLiteral("org.test.notinstalled");
    const QString zipPath = _writePackageZip(QStringLiteral("notinstalled.qgcplugin"), _validManifestJson(id),
                                             QStringLiteral("marker.txt"));
    QVERIFY(!zipPath.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("is not installed"));
    const PluginInstallResult result = PluginInstaller::stageUpdate(id, zipPath);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);

    // A fresh install is installFromFile()'s job; staging wrote nothing anywhere
    QVERIFY(!QFileInfo::exists(PluginInstaller::pendingUpdateDir(id)));
    QVERIFY(!QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(id)));
}

void PluginInstallerTest::_stageIncompatiblePackageRefused_test()
{
    const QString id = QStringLiteral("org.test.stageincompatible");
    const QString v1Zip = _writePackageZip(QStringLiteral("incompat-v1.qgcplugin"), _validManifestJson(id),
                                           QStringLiteral("v1-only.txt"));
    QJsonObject future = _validManifestJson(id, QStringLiteral("2.0.0"));
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("999.0");
    hostVersion[QStringLiteral("max")] = QString();
    future[QStringLiteral("hostVersion")] = hostVersion;
    const QString v2Zip =
        _writePackageZip(QStringLiteral("incompat-v2.qgcplugin"), future, QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("the update cannot load"));
    const PluginInstallResult result = PluginInstaller::stageUpdate(id, v2Zip);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);

    QVERIFY(!QFileInfo::exists(PluginInstaller::pendingUpdateDir(id)));
    QCOMPARE(_manifestVersionAt(QDir(PluginInstaller::userPluginsDir()).filePath(id)), QStringLiteral("1.0.0"));
}

void PluginInstallerTest::_applyInvalidPendingKeepsOldPackage_test()
{
    const QString id = QStringLiteral("org.test.badpending");
    const QString v1Zip = _writePackageZip(QStringLiteral("badpending-v1.qgcplugin"), _validManifestJson(id),
                                           QStringLiteral("v1-only.txt"));
    const QString v2Zip =
        _writePackageZip(QStringLiteral("badpending-v2.qgcplugin"), _validManifestJson(id, QStringLiteral("2.0.0")),
                         QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);
    QVERIFY(PluginInstaller::stageUpdate(id, v2Zip).success);

    // The staged copy is damaged on disk before the next start
    QFile pendingManifest(QDir(PluginInstaller::pendingUpdateDir(id)).filePath(QStringLiteral("qgcplugin.json")));
    QVERIFY(pendingManifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    pendingManifest.write("{ not json");
    pendingManifest.close();

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg,
                     QRegularExpression("not applied.*the staged update cannot load"));
    const QList<PluginInstallResult> results = PluginInstaller::applyPendingUpdates();
    verifyExpectedLogMessage();
    QCOMPARE(results.size(), 1);
    QVERIFY(!results.first().success);
    QCOMPARE(results.first().pluginId, id);

    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath(id);
    QCOMPARE(_manifestVersionAt(installedDir), QStringLiteral("1.0.0"));
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/v1-only.txt")));
    QVERIFY(!QFile::exists(installedDir + QStringLiteral("/v2-only.txt")));

    // Refused once, not at every start
    QVERIFY(!QFileInfo::exists(PluginInstaller::pendingUpdateDir(id)));
    QVERIFY(PluginInstaller::applyPendingUpdates().isEmpty());
}

void PluginInstallerTest::_applyPendingIdMismatchKeepsOldPackage_test()
{
    // A staged directory whose manifest names another plugin must not install over this one
    const QString id = QStringLiteral("org.test.mismatch");
    const QString v1Zip = _writePackageZip(QStringLiteral("mismatch-v1.qgcplugin"), _validManifestJson(id),
                                           QStringLiteral("v1-only.txt"));
    const QString v2Zip =
        _writePackageZip(QStringLiteral("mismatch-v2.qgcplugin"), _validManifestJson(id, QStringLiteral("2.0.0")),
                         QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);
    QVERIFY(PluginInstaller::stageUpdate(id, v2Zip).success);

    QFile pendingManifest(QDir(PluginInstaller::pendingUpdateDir(id)).filePath(QStringLiteral("qgcplugin.json")));
    QVERIFY(pendingManifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    pendingManifest.write(
        QJsonDocument(_validManifestJson(QStringLiteral("org.test.other"), QStringLiteral("2.0.0"))).toJson());
    pendingManifest.close();

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("declares id 'org.test.other'"));
    const QList<PluginInstallResult> results = PluginInstaller::applyPendingUpdates();
    verifyExpectedLogMessage();
    QCOMPARE(results.size(), 1);
    QVERIFY(!results.first().success);

    QCOMPARE(_manifestVersionAt(QDir(PluginInstaller::userPluginsDir()).filePath(id)), QStringLiteral("1.0.0"));
    QVERIFY(!QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(QStringLiteral("org.test.other"))));
}

void PluginInstallerTest::_applyFailedMoveKeepsOldPackage_test()
{
    // The failure is produced with POSIX directory permissions: Windows has no write bit
    // on a directory (setPermissions only sets the read-only attribute), and root ignores it.
#if defined(Q_OS_WIN)
    QSKIP("needs POSIX directory write permission to make a rename fail");
#else
    if (geteuid() == 0) {
        QSKIP("root ignores directory write permission, so the rename cannot be made to fail");
    }
#endif

    const QString id = QStringLiteral("org.test.lockedmove");
    const QString v1Zip =
        _writePackageZip(QStringLiteral("locked-v1.qgcplugin"), _validManifestJson(id), QStringLiteral("v1-only.txt"));
    const QString v2Zip =
        _writePackageZip(QStringLiteral("locked-v2.qgcplugin"), _validManifestJson(id, QStringLiteral("2.0.0")),
                         QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);
    QVERIFY(PluginInstaller::stageUpdate(id, v2Zip).success);

    // Moving a directory out of its parent needs write permission on that parent. With
    // the staging root read-only, the installed package is renamed aside and then the
    // staged one cannot follow — the failure between the two renames.
    const QString pendingRoot = QFileInfo(PluginInstaller::pendingUpdateDir(id)).absolutePath();
    const QFileDevice::Permissions originalPermissions = QFile::permissions(pendingRoot);
    QVERIFY(QFile::setPermissions(pendingRoot, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto restorePermissions = qScopeGuard([&] { QFile::setPermissions(pendingRoot, originalPermissions); });

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg,
                     QRegularExpression("not applied.*could not move the update into place"));
    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg,
                     QRegularExpression("Could not discard the staged update"));
    const QList<PluginInstallResult> results = PluginInstaller::applyPendingUpdates();
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    QCOMPARE(results.size(), 1);
    QVERIFY(!results.first().success);
    // This message, and not "could not move the installed package aside", is what says
    // the first rename happened and the old package had to be put back.
    QVERIFY2(results.first().errorString == QStringLiteral("could not move the update into place"),
             qPrintable(results.first().errorString));

    // The old package is back in place and whole
    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath(id);
    QCOMPARE(_manifestVersionAt(installedDir), QStringLiteral("1.0.0"));
    QVERIFY(QFile::exists(installedDir + QStringLiteral("/v1-only.txt")));
    QVERIFY(!QFile::exists(installedDir + QStringLiteral("/v2-only.txt")));
    QVERIFY(!QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(QStringLiteral(".previous/") + id)));
}

void PluginInstallerTest::_applyFinishesInterruptedSwap_test()
{
    const QString id = QStringLiteral("org.test.interrupted");
    const QString v1Zip = _writePackageZip(QStringLiteral("interrupted-v1.qgcplugin"), _validManifestJson(id),
                                           QStringLiteral("v1-only.txt"));
    const QString v2Zip =
        _writePackageZip(QStringLiteral("interrupted-v2.qgcplugin"), _validManifestJson(id, QStringLiteral("2.0.0")),
                         QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);
    QVERIFY(PluginInstaller::stageUpdate(id, v2Zip).success);

    // A run killed between the two renames: the old package is aside, nothing in its place
    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath(id);
    const QString previousDir = QDir(PluginInstaller::userPluginsDir()).filePath(QStringLiteral(".previous/") + id);
    QVERIFY(QDir().mkpath(QFileInfo(previousDir).absolutePath()));
    QVERIFY(QDir().rename(installedDir, previousDir));

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("after an interrupted update"));
    const QList<PluginInstallResult> results = PluginInstaller::applyPendingUpdates();
    verifyExpectedLogMessage();
    QCOMPARE(results.size(), 1);
    QVERIFY2(results.first().success, qPrintable(results.first().errorString));
    QCOMPARE(_manifestVersionAt(installedDir), QStringLiteral("2.0.0"));
    QVERIFY(!QFileInfo::exists(previousDir));
}

void PluginInstallerTest::_scanIgnoresPendingContent_test()
{
    const QString id = QStringLiteral("org.test.scanpending");
    const QString v1Zip =
        _writePackageZip(QStringLiteral("scan-v1.qgcplugin"), _validManifestJson(id), QStringLiteral("v1-only.txt"));
    const QString v2Zip =
        _writePackageZip(QStringLiteral("scan-v2.qgcplugin"), _validManifestJson(id, QStringLiteral("2.0.0")),
                         QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);
    QVERIFY(PluginInstaller::stageUpdate(id, v2Zip).success);

    // Also a staged package with no installed counterpart, written by hand
    const QString orphanDir = PluginInstaller::pendingUpdateDir(QStringLiteral("org.test.orphan"));
    QVERIFY(QDir().mkpath(orphanDir));
    QFile orphanManifest(QDir(orphanDir).filePath(QStringLiteral("qgcplugin.json")));
    QVERIFY(orphanManifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    orphanManifest.write(QJsonDocument(_validManifestJson(QStringLiteral("org.test.orphan"))).toJson());
    orphanManifest.close();

    // The scan finds the installed package and nothing staged
    const QList<PluginLoadInfo> found = QGCPluginLoader::inspectDirectories({PluginInstaller::userPluginsDir()});
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().manifest.id, id);
    QCOMPARE(found.first().manifest.version.toString(), QStringLiteral("1.0.0"));
    QCOMPARE(found.first().packageDir, QDir(PluginInstaller::userPluginsDir()).filePath(id));
}

void PluginInstallerTest::_stageMismatchedIdRefused_test()
{
    // Asked to update one installed plugin, handed a package for another installed one
    const QString id = QStringLiteral("org.test.updatetarget");
    const QString otherId = QStringLiteral("org.test.otherinstalled");
    const QString targetZip = _writePackageZip(QStringLiteral("target-v1.qgcplugin"), _validManifestJson(id),
                                               QStringLiteral("target-only.txt"));
    const QString otherZip = _writePackageZip(QStringLiteral("other-v1.qgcplugin"), _validManifestJson(otherId),
                                              QStringLiteral("other-v1-only.txt"));
    const QString otherV2Zip =
        _writePackageZip(QStringLiteral("other-v2.qgcplugin"), _validManifestJson(otherId, QStringLiteral("2.0.0")),
                         QStringLiteral("other-v2-only.txt"));
    QVERIFY(!targetZip.isEmpty() && !otherZip.isEmpty() && !otherV2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(targetZip).success);
    QVERIFY(PluginInstaller::installFromFile(otherZip).success);

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg,
                     QRegularExpression("package declares id 'org.test.otherinstalled', not 'org.test.updatetarget'"));
    const PluginInstallResult result = PluginInstaller::stageUpdate(id, otherV2Zip);
    verifyExpectedLogMessage();
    QVERIFY(!result.success);
    QCOMPARE(result.pluginId, id);

    // Refused before extraction: nothing staged for either id
    QVERIFY(!QFileInfo::exists(PluginInstaller::pendingUpdateDir(id)));
    QVERIFY(!QFileInfo::exists(PluginInstaller::pendingUpdateDir(otherId)));
    QVERIFY(PluginInstaller::applyPendingUpdates().isEmpty());
    QCOMPARE(_manifestVersionAt(QDir(PluginInstaller::userPluginsDir()).filePath(otherId)), QStringLiteral("1.0.0"));
}

void PluginInstallerTest::_applyWithNothingInstalledDiscards_test()
{
    // The plugin went away after its update was staged (a remove whose discard failed,
    // or a hand delete). The update must not install it again.
    const QString id = QStringLiteral("org.test.removedafterstage");
    const QString v1Zip =
        _writePackageZip(QStringLiteral("removed-v1.qgcplugin"), _validManifestJson(id), QStringLiteral("v1-only.txt"));
    const QString v2Zip =
        _writePackageZip(QStringLiteral("removed-v2.qgcplugin"), _validManifestJson(id, QStringLiteral("2.0.0")),
                         QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);
    QVERIFY(PluginInstaller::stageUpdate(id, v2Zip).success);
    const QString installedDir = QDir(PluginInstaller::userPluginsDir()).filePath(id);
    QVERIFY(QDir(installedDir).removeRecursively());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg, QRegularExpression("not applied.*is not installed"));
    const QList<PluginInstallResult> results = PluginInstaller::applyPendingUpdates();
    verifyExpectedLogMessage();
    QCOMPARE(results.size(), 1);
    QVERIFY(!results.first().success);

    QVERIFY(!QFileInfo::exists(installedDir));
    QVERIFY(!QFileInfo::exists(PluginInstaller::pendingUpdateDir(id)));
}

void PluginInstallerTest::_installUnsafeIdRejected_test()
{
    // A staged update that a manual install with id ".pending" would otherwise delete
    const QString stagedId = QStringLiteral("org.test.keepstaged");
    const QString v1Zip = _writePackageZip(QStringLiteral("keep-v1.qgcplugin"), _validManifestJson(stagedId),
                                           QStringLiteral("v1-only.txt"));
    const QString v2Zip =
        _writePackageZip(QStringLiteral("keep-v2.qgcplugin"), _validManifestJson(stagedId, QStringLiteral("2.0.0")),
                         QStringLiteral("v2-only.txt"));
    QVERIFY(!v1Zip.isEmpty() && !v2Zip.isEmpty());
    QVERIFY(PluginInstaller::installFromFile(v1Zip).success);
    QVERIFY(PluginInstaller::stageUpdate(stagedId, v2Zip).success);

    // A file beside the plugins directory, which an id of ".." would otherwise reach
    const QString besidePlugins = QFileInfo(PluginInstaller::userPluginsDir()).absolutePath();
    QFile sentinel(QDir(besidePlugins).filePath(QStringLiteral("unsafe-id-sentinel.txt")));
    QVERIFY(sentinel.open(QIODevice::WriteOnly | QIODevice::Truncate));
    sentinel.write("keep");
    sentinel.close();

    const QStringList unsafeIds = {QStringLiteral(".."), QStringLiteral(".pending"), QStringLiteral(".previous")};
    for (const QString& unsafeId : unsafeIds) {
        const QString zipPath = _writePackageZip(QStringLiteral("unsafe-id.qgcplugin"), _validManifestJson(unsafeId),
                                                 QStringLiteral("payload.txt"));
        QVERIFY(!zipPath.isEmpty());

        expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg,
                         QRegularExpression("cannot name a package directory"));
        const PluginInstallResult result = PluginInstaller::installFromFile(zipPath);
        verifyExpectedLogMessage();
        QVERIFY2(!result.success, qPrintable(unsafeId));
    }

    QVERIFY(QFile::exists(sentinel.fileName()));
    QCOMPARE(_manifestVersionAt(PluginInstaller::pendingUpdateDir(stagedId)), QStringLiteral("2.0.0"));
    QVERIFY(!QFile::exists(QDir(PluginInstaller::userPluginsDir()).filePath(QStringLiteral(".pending/payload.txt"))));
    QVERIFY(QFile::remove(sentinel.fileName()));
}

void PluginInstallerTest::_readManifestRunsInstallChecks_test()
{
    // A caller checks a package through this before it installs it, so it must reject
    // what an install rejects, with the same reason, and write nothing.
    const QString goodZip =
        _writePackageZip(QStringLiteral("readmanifest.qgcplugin"), _validManifestJson(QStringLiteral("org.test.read")),
                         QStringLiteral("marker.txt"));
    QVERIFY(!goodZip.isEmpty());
    QString error;
    const PluginManifest manifest = PluginInstaller::readManifest(goodZip, &error);
    QCOMPARE(manifest.id, QStringLiteral("org.test.read"));
    QCOMPARE(manifest.version, QVersionNumber(1, 0, 0));
    QVERIFY(error.isEmpty());

    QMap<QString, QByteArray> entries;
    entries[QStringLiteral("qml/View.qml")] = QByteArrayLiteral("import QtQuick\nItem {}\n");
    const QString manifestless = _writeZip(QStringLiteral("readmanifest-none.qgcplugin"), entries);
    QVERIFY(!manifestless.isEmpty());

    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg,
                     QRegularExpression("archive does not contain qgcplugin.json at its root"));
    QVERIFY(PluginInstaller::readManifest(manifestless, &error).id.isEmpty());
    verifyExpectedLogMessage();
    expectLogMessage("PluginSystem.PluginInstaller", QtWarningMsg,
                     QRegularExpression("archive does not contain qgcplugin.json at its root"));
    const PluginInstallResult install = PluginInstaller::installFromFile(manifestless);
    verifyExpectedLogMessage();
    QVERIFY(!install.success);
    QCOMPARE(error, install.errorString);
    QVERIFY(!QFileInfo::exists(QDir(PluginInstaller::userPluginsDir()).filePath(QStringLiteral("org.test.read"))));
}

UT_REGISTER_TEST(PluginInstallerTest, TestLabel::Unit)
