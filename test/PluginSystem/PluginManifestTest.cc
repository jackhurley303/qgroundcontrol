#include "PluginManifestTest.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include "PluginManifest.h"

namespace {

QJsonObject validInternalManifestJson()
{
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("org.example.qgc.example");
    json[QStringLiteral("name")] = QStringLiteral("Example");
    json[QStringLiteral("version")] = QStringLiteral("1.0.0");
    json[QStringLiteral("vendor")] = QStringLiteral("Example Org");
    json[QStringLiteral("description")] = QStringLiteral("Demonstrates the QGC plugin system");
    json[QStringLiteral("tier")] = QStringLiteral("internal");
    json[QStringLiteral("apiVersion")] = 1;
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    hostVersion[QStringLiteral("max")] = QString();
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("hostBuildId")] = QStringLiteral("abc1234");
    json[QStringLiteral("contributes")] = QJsonObject();
    return json;
}

} // namespace

void PluginManifestTest::_malformedJson_test()
{
    const QByteArray malformed = QByteArrayLiteral("{ this is not valid json ");
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(malformed, &parseError);
    QVERIFY(parseError.error != QJsonParseError::NoError);
    QVERIFY(doc.isNull());
}

void PluginManifestTest::_missingRequiredFields_test()
{
    QString error;

    QJsonObject missingId = validInternalManifestJson();
    missingId.remove(QStringLiteral("id"));
    QVERIFY(PluginManifest::fromJson(missingId, &error).id.isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QJsonObject missingVersion = validInternalManifestJson();
    missingVersion.remove(QStringLiteral("version"));
    QVERIFY(PluginManifest::fromJson(missingVersion, &error).id.isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QJsonObject missingApiVersion = validInternalManifestJson();
    missingApiVersion.remove(QStringLiteral("apiVersion"));
    QVERIFY(PluginManifest::fromJson(missingApiVersion, &error).id.isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QJsonObject missingHostVersion = validInternalManifestJson();
    missingHostVersion.remove(QStringLiteral("hostVersion"));
    QVERIFY(PluginManifest::fromJson(missingHostVersion, &error).id.isEmpty());
    QVERIFY(!error.isEmpty());
}

void PluginManifestTest::_badTier_test()
{
    QJsonObject json = validInternalManifestJson();
    json[QStringLiteral("tier")] = QStringLiteral("nonsense");

    QString error;
    const PluginManifest manifest = PluginManifest::fromJson(json, &error);
    QVERIFY(manifest.id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("tier")));
}

void PluginManifestTest::_hostVersionMinEqualsHost_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = 1;
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(manifest.validateForHost(host, &reason));
    QVERIFY(reason.isEmpty());
}

void PluginManifestTest::_hostVersionMaxEqualsHostRejected_test()
{
    QJsonObject json = validInternalManifestJson();
    QJsonObject hostVersion = json.value(QStringLiteral("hostVersion")).toObject();
    hostVersion[QStringLiteral("max")] = QStringLiteral("6.0");
    json[QStringLiteral("hostVersion")] = hostVersion;

    const PluginManifest manifest = PluginManifest::fromJson(json);
    QVERIFY(!manifest.id.isEmpty());

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("6.0"));
    host.apiVersion = 1;
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(!manifest.validateForHost(host, &reason));
    QVERIFY(!reason.isEmpty());
}

void PluginManifestTest::_hostVersionEmptyMaxUnbounded_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());
    QVERIFY(manifest.hostVersionMax.isNull());

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("99.0"));
    host.apiVersion = 1;
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(manifest.validateForHost(host, &reason));
    QVERIFY(reason.isEmpty());
}

void PluginManifestTest::_hostVersionBelowMinRejected_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("4.9"));
    host.apiVersion = 1;
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(!manifest.validateForHost(host, &reason));
    QVERIFY(!reason.isEmpty());
}

void PluginManifestTest::_internalTierHashMatch_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());
    QCOMPARE(manifest.tier, PluginManifest::Tier::Internal);

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = 1;
    host.buildId = QStringLiteral("abc1234");

    QVERIFY(manifest.validateForHost(host));
}

void PluginManifestTest::_internalTierHashMismatch_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = 1;
    host.buildId = QStringLiteral("def5678");

    QString reason;
    QVERIFY(!manifest.validateForHost(host, &reason));
    QVERIFY(reason.contains(QStringLiteral("build")));
}

void PluginManifestTest::_internalTierMissingHashInvalid_test()
{
    QJsonObject json = validInternalManifestJson();
    json.remove(QStringLiteral("hostBuildId"));

    QString error;
    const PluginManifest manifest = PluginManifest::fromJson(json, &error);
    QVERIFY(manifest.id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("hostBuildId")));
}

void PluginManifestTest::_apiVersionMismatch_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = 2;
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(!manifest.validateForHost(host, &reason));
    QVERIFY(reason.contains(QStringLiteral("apiVersion")));
}

UT_REGISTER_TEST(PluginManifestTest, TestLabel::Unit)
