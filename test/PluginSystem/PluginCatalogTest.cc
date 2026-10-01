#include "PluginCatalogTest.h"

#include <limits>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include "PluginCatalog.h"
#include "QGCPluginInterface.h"

namespace {

const QString MacKey = QStringLiteral("macos-universal");
const QString WinKey = QStringLiteral("windows-x64");
const QString Sha = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

QJsonObject packageJson(const QString& name = QStringLiteral("pkg"))
{
    QJsonObject json;
    json[QStringLiteral("url")] = QStringLiteral("https://example.org/releases/%1.qgcplugin").arg(name);
    json[QStringLiteral("sha256")] = Sha;
    json[QStringLiteral("size")] = 1024;
    return json;
}

QJsonObject hostVersionJson(const QString& min, const QString& max = QString())
{
    QJsonObject json;
    json[QStringLiteral("min")] = min;
    json[QStringLiteral("max")] = max;
    return json;
}

QJsonObject sdkVersionJson(const QString& version = QStringLiteral("1.0.0"))
{
    QJsonObject packages;
    packages[MacKey] = packageJson(version);

    QJsonObject json;
    json[QStringLiteral("version")] = version;
    json[QStringLiteral("tier")] = QStringLiteral("sdk");
    json[QStringLiteral("apiVersion")] = QGCPluginApiVersion;
    json[QStringLiteral("hostVersion")] = hostVersionJson(QStringLiteral("5.0"));
    json[QStringLiteral("released")] = QStringLiteral("2026-09-30");
    json[QStringLiteral("notes")] = QStringLiteral("First release");
    json[QStringLiteral("packages")] = packages;
    return json;
}

QJsonObject qmlVersionJson(const QString& version = QStringLiteral("1.0.0"))
{
    QJsonObject packages;
    packages[QStringLiteral("any")] = packageJson(version);

    QJsonObject json;
    json[QStringLiteral("version")] = version;
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    json[QStringLiteral("hostVersion")] = hostVersionJson(QStringLiteral("5.0"));
    json[QStringLiteral("packages")] = packages;
    return json;
}

QJsonObject pluginJson(const QString& id, const QJsonArray& versions)
{
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("name")] = QStringLiteral("Example Plugin");
    json[QStringLiteral("author")] = QStringLiteral("Example Org");
    json[QStringLiteral("summary")] = QStringLiteral("Does a thing");
    json[QStringLiteral("description")] = QStringLiteral("Does a thing, in more words");
    json[QStringLiteral("license")] = QStringLiteral("Apache-2.0");
    json[QStringLiteral("homepage")] = QStringLiteral("https://example.org");
    json[QStringLiteral("repository")] = QStringLiteral("https://example.org/repo");
    json[QStringLiteral("icon")] = QStringLiteral("https://example.org/icon.png");
    json[QStringLiteral("screenshots")] = QJsonArray{QStringLiteral("https://example.org/one.png")};
    json[QStringLiteral("versions")] = versions;
    return json;
}

QJsonObject indexJson(const QJsonArray& plugins, int schemaVersion = 1)
{
    QJsonObject json;
    json[QStringLiteral("schemaVersion")] = schemaVersion;
    json[QStringLiteral("generated")] = QStringLiteral("2026-09-30");
    json[QStringLiteral("plugins")] = plugins;
    return json;
}

// An index holding one plugin with one version.
QJsonObject singleVersionIndex(const QJsonObject& version)
{
    return indexJson(QJsonArray{pluginJson(QStringLiteral("org.example.one"), QJsonArray{version})});
}

HostInfo hostWithVersion(const QString& version)
{
    HostInfo host;
    host.version = QVersionNumber::fromString(version);
    host.apiVersion = QGCPluginApiVersion;
    host.qmlApiVersion = QGCPluginQmlApiLevel;
    return host;
}

}  // namespace

void PluginCatalogTest::_parseValidIndex_test()
{
    const QJsonObject json = indexJson(QJsonArray{
        pluginJson(QStringLiteral("org.example.native"), QJsonArray{sdkVersionJson()}),
        pluginJson(QStringLiteral("org.example.qml"), QJsonArray{qmlVersionJson()}),
    });

    QString error;
    const PluginCatalog catalog = PluginCatalog::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(catalog.schemaVersion, 1);
    QCOMPARE(catalog.generated, QStringLiteral("2026-09-30"));
    QVERIFY(!catalog.needsNewerHost());
    QCOMPARE(catalog.plugins.size(), 2);

    const PluginCatalogEntry& entry = catalog.plugins.at(0);
    QCOMPARE(entry.id, QStringLiteral("org.example.native"));
    QCOMPARE(entry.name, QStringLiteral("Example Plugin"));
    QCOMPARE(entry.author, QStringLiteral("Example Org"));
    QCOMPARE(entry.summary, QStringLiteral("Does a thing"));
    QCOMPARE(entry.license, QStringLiteral("Apache-2.0"));
    QCOMPARE(entry.repository, QStringLiteral("https://example.org/repo"));
    QCOMPARE(entry.screenshots, QStringList{QStringLiteral("https://example.org/one.png")});
    QCOMPARE(entry.versions.size(), 1);

    const PluginCatalogVersion& version = entry.versions.at(0);
    QCOMPARE(version.manifest.tier, PluginManifest::Tier::Sdk);
    QCOMPARE(version.manifest.version, QVersionNumber(1, 0, 0));
    QCOMPARE(version.manifest.hostVersionMin, QVersionNumber(5, 0));
    QVERIFY(version.manifest.hostVersionMax.isNull());
    QCOMPARE(version.released, QStringLiteral("2026-09-30"));
    QCOMPARE(version.notes, QStringLiteral("First release"));
    QVERIFY(version.packages.contains(MacKey));
    QCOMPARE(version.packages.value(MacKey).sha256, Sha);
    QCOMPARE(version.packages.value(MacKey).size, qint64(1024));

    const QByteArray data = QJsonDocument(json).toJson();
    QString dataError;
    QCOMPARE(PluginCatalog::fromData(data, &dataError).plugins.size(), 2);
    QVERIFY2(dataError.isEmpty(), qPrintable(dataError));
}

void PluginCatalogTest::_unknownFieldsAndPackageKeysIgnored_test()
{
    QJsonObject version = sdkVersionJson();
    QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
    packages[QStringLiteral("haiku-riscv")] = QStringLiteral("not even an object");
    packages[MacKey] = [] {
        QJsonObject pkg = packageJson();
        pkg[QStringLiteral("signature")] = QStringLiteral("future field");
        return pkg;
    }();
    version[QStringLiteral("packages")] = packages;
    version[QStringLiteral("futureField")] = 42;

    QJsonObject plugin = pluginJson(QStringLiteral("org.example.one"), QJsonArray{version});
    plugin[QStringLiteral("rating")] = 5;

    QJsonObject json = indexJson(QJsonArray{plugin});
    json[QStringLiteral("signedBy")] = QStringLiteral("someone");

    QString error;
    const PluginCatalog catalog = PluginCatalog::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(catalog.plugins.size(), 1);
    const QHash<QString, PluginCatalogPackage>& parsed = catalog.plugins.at(0).versions.at(0).packages;
    QCOMPARE(parsed.size(), 1);
    QVERIFY(parsed.contains(MacKey));
}

void PluginCatalogTest::_authorFillsManifestVendor_test()
{
    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(sdkVersionJson()));
    QCOMPARE(catalog.plugins.size(), 1);
    QCOMPARE(catalog.plugins.at(0).versions.at(0).manifest.vendor, QStringLiteral("Example Org"));
    QCOMPARE(catalog.plugins.at(0).versions.at(0).manifest.id, QStringLiteral("org.example.one"));
}

void PluginCatalogTest::_schemaVersionNewerRefused_test()
{
    QString error;
    const PluginCatalog catalog = PluginCatalog::fromJson(
        indexJson(QJsonArray{pluginJson(QStringLiteral("org.example.one"), QJsonArray{sdkVersionJson()})}, 2), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("schemaVersion")));
    QVERIFY(catalog.needsNewerHost());
    QCOMPARE(catalog.schemaVersion, 2);
    QVERIFY(catalog.plugins.isEmpty());
}

void PluginCatalogTest::_schemaVersionMissingOrInvalidRefused_test()
{
    QString error;

    QJsonObject missing = indexJson(QJsonArray());
    missing.remove(QStringLiteral("schemaVersion"));
    QVERIFY(PluginCatalog::fromJson(missing, &error).plugins.isEmpty());
    QVERIFY(error.contains(QStringLiteral("schemaVersion")));

    error.clear();
    QCOMPARE(PluginCatalog::fromJson(indexJson(QJsonArray(), 0), &error).schemaVersion, 0);
    QVERIFY(error.contains(QStringLiteral("schemaVersion")));

    error.clear();
    QJsonObject fractional = indexJson(QJsonArray());
    fractional[QStringLiteral("schemaVersion")] = 1.5;
    QCOMPARE(PluginCatalog::fromJson(fractional, &error).schemaVersion, 0);
    QVERIFY(error.contains(QStringLiteral("schemaVersion")));

    error.clear();
    QJsonObject noPlugins = indexJson(QJsonArray());
    noPlugins.remove(QStringLiteral("plugins"));
    PluginCatalog::fromJson(noPlugins, &error);
    QVERIFY(error.contains(QStringLiteral("plugins")));
}

void PluginCatalogTest::_malformedData_test()
{
    QString error;
    QVERIFY(PluginCatalog::fromData(QByteArrayLiteral("{ this is not valid json "), &error).plugins.isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(PluginCatalog::fromData(QByteArrayLiteral("[1, 2]"), &error).plugins.isEmpty());
    QVERIFY(error.contains(QStringLiteral("object")));
}

void PluginCatalogTest::_missingSha256Refused_test()
{
    QJsonObject version = sdkVersionJson();
    QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
    QJsonObject package = packages.value(MacKey).toObject();
    package.remove(QStringLiteral("sha256"));
    packages[MacKey] = package;
    version[QStringLiteral("packages")] = packages;

    QString error;
    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(version), &error);
    QVERIFY(catalog.plugins.isEmpty());
    QVERIFY(error.contains(QStringLiteral("sha256")));
}

void PluginCatalogTest::_badSha256Refused_test()
{
    for (const QString& bad : {QStringLiteral("abc123"), QString(64, QLatin1Char('z'))}) {
        QJsonObject version = sdkVersionJson();
        QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
        QJsonObject package = packages.value(MacKey).toObject();
        package[QStringLiteral("sha256")] = bad;
        packages[MacKey] = package;
        version[QStringLiteral("packages")] = packages;

        QString error;
        QVERIFY(PluginCatalog::fromJson(singleVersionIndex(version), &error).plugins.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("sha256")), qPrintable(error));
    }

    // Uppercase hex is accepted and normalised, since hash comparison is on lowercase.
    QJsonObject version = sdkVersionJson();
    QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
    QJsonObject package = packages.value(MacKey).toObject();
    package[QStringLiteral("sha256")] = Sha.toUpper();
    packages[MacKey] = package;
    version[QStringLiteral("packages")] = packages;
    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(version));
    QCOMPARE(catalog.plugins.size(), 1);
    QCOMPARE(catalog.plugins.at(0).versions.at(0).packages.value(MacKey).sha256, Sha);
}

void PluginCatalogTest::_missingUrlOrSizeRefused_test()
{
    for (const QString& field : {QStringLiteral("url"), QStringLiteral("size")}) {
        QJsonObject version = sdkVersionJson();
        QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
        QJsonObject package = packages.value(MacKey).toObject();
        package.remove(field);
        packages[MacKey] = package;
        version[QStringLiteral("packages")] = packages;

        QString error;
        QVERIFY(PluginCatalog::fromJson(singleVersionIndex(version), &error).plugins.isEmpty());
        QVERIFY2(error.contains(field), qPrintable(error));
    }
}

void PluginCatalogTest::_unparseableVersionRefused_test()
{
    QJsonObject version = sdkVersionJson(QStringLiteral("not-a-version"));

    QString error;
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(version), &error).plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("version")), qPrintable(error));

    error.clear();
    QJsonObject missing = sdkVersionJson();
    missing.remove(QStringLiteral("version"));
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(missing), &error).plugins.isEmpty());
    QVERIFY(error.contains(QStringLiteral("version")));

    error.clear();
    QJsonObject badHostMin = sdkVersionJson();
    badHostMin[QStringLiteral("hostVersion")] = hostVersionJson(QStringLiteral("five"));
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(badHostMin), &error).plugins.isEmpty());
    QVERIFY(error.contains(QStringLiteral("hostVersion")));

    error.clear();
    QJsonObject badHostMax = sdkVersionJson();
    badHostMax[QStringLiteral("hostVersion")] = hostVersionJson(QStringLiteral("5.0"), QStringLiteral("six"));
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(badHostMax), &error).plugins.isEmpty());
    QVERIFY(error.contains(QStringLiteral("hostVersion.max")));
}

void PluginCatalogTest::_versionSuffixRefused_test()
{
    // QVersionNumber::fromString("1.0.0-beta") yields 1.0.0, which would equal the final release.
    QString error;
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(sdkVersionJson(QStringLiteral("1.0.0-beta"))), &error)
                .plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("1.0.0-beta")), qPrintable(error));
}

void PluginCatalogTest::_anyPackageOnSdkTierRefused_test()
{
    QJsonObject version = sdkVersionJson();
    QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
    packages[QStringLiteral("any")] = packageJson();
    version[QStringLiteral("packages")] = packages;

    QString error;
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(version), &error).plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("any")), qPrintable(error));
}

void PluginCatalogTest::_anyPackageOnQmlTierAccepted_test()
{
    QString error;
    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(qmlVersionJson()), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(catalog.plugins.at(0).versions.at(0).packages.contains(QStringLiteral("any")));

    // Any platform key resolves the `any` package.
    const std::optional<PluginCatalogOffer> offer =
        catalog.plugins.at(0).newestCompatible(hostWithVersion(QStringLiteral("5.1")), WinKey);
    QVERIFY(offer.has_value());
    QCOMPARE(offer->package.url, QStringLiteral("https://example.org/releases/1.0.0.qgcplugin"));
}

void PluginCatalogTest::_internalTierRefused_test()
{
    QJsonObject version = sdkVersionJson();
    version[QStringLiteral("tier")] = QStringLiteral("internal");
    version[QStringLiteral("hostBuildId")] = QStringLiteral("abc1234");

    QString error;
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(version), &error).plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("tier")), qPrintable(error));
}

void PluginCatalogTest::_sdkMissingApiVersionRefused_test()
{
    QJsonObject version = sdkVersionJson();
    version.remove(QStringLiteral("apiVersion"));

    QString error;
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(version), &error).plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("apiVersion")), qPrintable(error));
}

void PluginCatalogTest::_duplicateIdsRefused_test()
{
    QString error;
    const QJsonObject dupPlugins = indexJson(QJsonArray{
        pluginJson(QStringLiteral("org.example.one"), QJsonArray{sdkVersionJson()}),
        pluginJson(QStringLiteral("org.example.one"), QJsonArray{sdkVersionJson()}),
    });
    QVERIFY(PluginCatalog::fromJson(dupPlugins, &error).plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("duplicate")), qPrintable(error));

    error.clear();
    const QJsonObject dupVersions = indexJson(QJsonArray{
        pluginJson(QStringLiteral("org.example.one"), QJsonArray{sdkVersionJson(), sdkVersionJson()}),
    });
    QVERIFY(PluginCatalog::fromJson(dupVersions, &error).plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("duplicate")), qPrintable(error));
}

void PluginCatalogTest::_hostVersionOutOfRangeHidden_test()
{
    QJsonObject version = sdkVersionJson();
    version[QStringLiteral("hostVersion")] = hostVersionJson(QStringLiteral("5.1"), QStringLiteral("6.0"));

    QString error;
    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(version), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const PluginCatalogEntry& entry = catalog.plugins.at(0);

    QVERIFY(!entry.newestCompatible(hostWithVersion(QStringLiteral("5.0")), MacKey).has_value());
    QVERIFY(entry.newestCompatible(hostWithVersion(QStringLiteral("5.1")), MacKey).has_value());
    QVERIFY(entry.newestCompatible(hostWithVersion(QStringLiteral("5.9")), MacKey).has_value());
    QVERIFY(!entry.newestCompatible(hostWithVersion(QStringLiteral("6.0")), MacKey).has_value());
}

void PluginCatalogTest::_apiVersionMismatchHidden_test()
{
    QJsonObject version = sdkVersionJson();
    version[QStringLiteral("apiVersion")] = QGCPluginApiVersion + 1;

    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(version));
    QCOMPARE(catalog.plugins.size(), 1);
    QVERIFY(!catalog.plugins.at(0).newestCompatible(hostWithVersion(QStringLiteral("5.1")), MacKey).has_value());
}

void PluginCatalogTest::_noPackageForPlatformHidden_test()
{
    QString error;
    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(sdkVersionJson()), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(catalog.plugins.size(), 1);

    const PluginCatalogEntry& entry = catalog.plugins.at(0);
    QVERIFY(entry.newestCompatible(hostWithVersion(QStringLiteral("5.1")), MacKey).has_value());
    QVERIFY(!entry.newestCompatible(hostWithVersion(QStringLiteral("5.1")), WinKey).has_value());
}

void PluginCatalogTest::_newestCompatibleSkipsIncompatibleNewer_test()
{
    QJsonObject newer = sdkVersionJson(QStringLiteral("2.0.0"));
    newer[QStringLiteral("hostVersion")] = hostVersionJson(QStringLiteral("9.0"));
    const QJsonObject older = sdkVersionJson(QStringLiteral("1.5.0"));

    const PluginCatalog catalog = PluginCatalog::fromJson(indexJson(QJsonArray{
        pluginJson(QStringLiteral("org.example.one"), QJsonArray{newer, older}),
    }));
    QCOMPARE(catalog.plugins.size(), 1);

    const std::optional<PluginCatalogOffer> offer =
        catalog.plugins.at(0).newestCompatible(hostWithVersion(QStringLiteral("5.1")), MacKey);
    QVERIFY(offer.has_value());
    QCOMPARE(offer->version.manifest.version, QVersionNumber(1, 5, 0));
}

void PluginCatalogTest::_newestIsNumericNotLexical_test()
{
    const PluginCatalog catalog = PluginCatalog::fromJson(indexJson(QJsonArray{
        pluginJson(QStringLiteral("org.example.one"),
                   QJsonArray{sdkVersionJson(QStringLiteral("1.9.0")), sdkVersionJson(QStringLiteral("1.10.0")),
                              sdkVersionJson(QStringLiteral("1.2.0"))}),
    }));
    QCOMPARE(catalog.plugins.size(), 1);

    const std::optional<PluginCatalogOffer> offer =
        catalog.plugins.at(0).newestCompatible(hostWithVersion(QStringLiteral("5.1")), MacKey);
    QVERIFY(offer.has_value());
    QCOMPARE(offer->version.manifest.version, QVersionNumber(1, 10, 0));
}

void PluginCatalogTest::_platformPackagePreferredOverAny_test()
{
    QJsonObject version = qmlVersionJson();
    QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
    packages[MacKey] = packageJson(QStringLiteral("mac-only"));
    version[QStringLiteral("packages")] = packages;

    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(version));
    QCOMPARE(catalog.plugins.size(), 1);
    const PluginCatalogEntry& entry = catalog.plugins.at(0);

    const std::optional<PluginCatalogOffer> mac =
        entry.newestCompatible(hostWithVersion(QStringLiteral("5.1")), MacKey);
    QVERIFY(mac.has_value());
    QVERIFY(mac->package.url.endsWith(QStringLiteral("mac-only.qgcplugin")));

    const std::optional<PluginCatalogOffer> win =
        entry.newestCompatible(hostWithVersion(QStringLiteral("5.1")), WinKey);
    QVERIFY(win.has_value());
    QVERIFY(win->package.url.endsWith(QStringLiteral("1.0.0.qgcplugin")));
}

void PluginCatalogTest::_updateForComparesInstalled_test()
{
    const PluginCatalog catalog = PluginCatalog::fromJson(indexJson(QJsonArray{
        pluginJson(QStringLiteral("org.example.one"),
                   QJsonArray{sdkVersionJson(QStringLiteral("1.0.0")), sdkVersionJson(QStringLiteral("1.2.0"))}),
    }));
    QCOMPARE(catalog.plugins.size(), 1);
    const PluginCatalogEntry& entry = catalog.plugins.at(0);
    const HostInfo host = hostWithVersion(QStringLiteral("5.1"));

    const std::optional<PluginCatalogOffer> update = entry.updateFor(host, MacKey, QVersionNumber(1, 0, 0));
    QVERIFY(update.has_value());
    QCOMPARE(update->version.manifest.version, QVersionNumber(1, 2, 0));

    QVERIFY(!entry.updateFor(host, MacKey, QVersionNumber(1, 2, 0)).has_value());
    QVERIFY(!entry.updateFor(host, MacKey, QVersionNumber(1, 3, 0)).has_value());
    QVERIFY(!entry.updateFor(host, WinKey, QVersionNumber(1, 0, 0)).has_value());
}

void PluginCatalogTest::_find_test()
{
    const PluginCatalog catalog = PluginCatalog::fromJson(singleVersionIndex(sdkVersionJson()));
    QVERIFY(catalog.find(QStringLiteral("org.example.one")) != nullptr);
    QVERIFY(catalog.find(QStringLiteral("org.example.missing")) == nullptr);
}

void PluginCatalogTest::_nonAsciiDigitSha256Refused_test()
{
    QJsonObject version = sdkVersionJson();
    QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
    QJsonObject package = packages.value(MacKey).toObject();
    package[QStringLiteral("sha256")] = QString(64, QChar(0xFF11));  // fullwidth digit one
    packages[MacKey] = package;
    version[QStringLiteral("packages")] = packages;

    QString error;
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(version), &error).plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("sha256")), qPrintable(error));
}

void PluginCatalogTest::_hugeNumbersRefused_test()
{
    QJsonObject version = sdkVersionJson();
    QJsonObject packages = version.value(QStringLiteral("packages")).toObject();
    QJsonObject package = packages.value(MacKey).toObject();
    package[QStringLiteral("size")] = 1e300;
    packages[MacKey] = package;
    version[QStringLiteral("packages")] = packages;

    QString error;
    QVERIFY(PluginCatalog::fromJson(singleVersionIndex(version), &error).plugins.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("size")), qPrintable(error));

    // An absurd schemaVersion reads as "too new", clamped to INT_MAX, not as undefined behaviour.
    error.clear();
    QJsonObject json = indexJson(QJsonArray());
    json[QStringLiteral("schemaVersion")] = 1e300;
    const PluginCatalog catalog = PluginCatalog::fromJson(json, &error);
    QVERIFY(catalog.needsNewerHost());
    QCOMPARE(catalog.schemaVersion, std::numeric_limits<int>::max());
    QVERIFY(catalog.plugins.isEmpty());
    QVERIFY(!error.isEmpty());
}

UT_REGISTER_TEST(PluginCatalogTest, TestLabel::Unit)
