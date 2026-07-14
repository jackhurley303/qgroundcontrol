#include "PluginManifestTest.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include "PluginManifest.h"
#include "QGCPluginInterface.h"
#include "QGCPluginLoader.h"

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
    json[QStringLiteral("apiVersion")] = QGCPluginApiVersion;
    QJsonObject hostVersion;
    hostVersion[QStringLiteral("min")] = QStringLiteral("5.0");
    hostVersion[QStringLiteral("max")] = QString();
    json[QStringLiteral("hostVersion")] = hostVersion;
    json[QStringLiteral("hostBuildId")] = QStringLiteral("abc1234");
    json[QStringLiteral("contributes")] = QJsonObject();
    return json;
}

// Mirrors the QPluginLoader::metaData() envelope shape: {IID, className, MetaData, ...}
QJsonObject validMetaDataEnvelope()
{
    QJsonObject envelope;
    envelope[QStringLiteral("IID")] = QStringLiteral(QGCPluginInterface_iid);
    envelope[QStringLiteral("className")] = QStringLiteral("ExamplePlugin");
    envelope[QStringLiteral("MetaData")] = validInternalManifestJson();
    return envelope;
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
    host.apiVersion = QGCPluginApiVersion;
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
    host.apiVersion = QGCPluginApiVersion;
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
    host.apiVersion = QGCPluginApiVersion;
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
    host.apiVersion = QGCPluginApiVersion;
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(!manifest.validateForHost(host, &reason));
    QVERIFY(!reason.isEmpty());
}

void PluginManifestTest::_nullHostVersionSkipsRangeCheck_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());

    // Tagless checkout: git describe yields a bare hash, which parses to a null version
    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("a1b2c3d"));
    host.apiVersion = QGCPluginApiVersion;
    host.buildId = QStringLiteral("abc1234");
    QVERIFY(host.version.isNull());

    QString reason;
    QVERIFY(manifest.validateForHost(host, &reason));
    QVERIFY(reason.isEmpty());
}

void PluginManifestTest::_internalTierHashMatch_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());
    QCOMPARE(manifest.tier, PluginManifest::Tier::Internal);

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = QGCPluginApiVersion;
    host.buildId = QStringLiteral("abc1234");

    QVERIFY(manifest.validateForHost(host));
}

void PluginManifestTest::_internalTierHashMismatch_test()
{
    const PluginManifest manifest = PluginManifest::fromJson(validInternalManifestJson());
    QVERIFY(!manifest.id.isEmpty());

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = QGCPluginApiVersion;
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
    host.apiVersion = QGCPluginApiVersion + 1;
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(!manifest.validateForHost(host, &reason));
    QVERIFY(reason.contains(QStringLiteral("apiVersion")));
}

void PluginManifestTest::_qmlTierApiVersionOptionalAndUnchecked_test()
{
    // Tier qml (Stage 3 packages) carries no binary and never touches the C++ ABI (D1);
    // apiVersion is optional at parse time and unchecked at validation time (F9).
    QJsonObject json = validInternalManifestJson();
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    json.remove(QStringLiteral("apiVersion"));
    json.remove(QStringLiteral("hostBuildId"));

    QString error;
    const PluginManifest manifest = PluginManifest::fromJson(json, &error);
    QVERIFY2(!manifest.id.isEmpty(), qPrintable(error));
    QCOMPARE(manifest.tier, PluginManifest::Tier::Qml);
    QCOMPARE(manifest.apiVersion, 0);

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = QGCPluginApiVersion + 1; // deliberately mismatched
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(manifest.validateForHost(host, &reason));
    QVERIFY(reason.isEmpty());
}

void PluginManifestTest::_qmlApiVersionChecked_test()
{
    // Declared qmlApiVersion (U3.1) is the qml-tier compatibility axis: checked against
    // the host's QGCPluginQmlApiLevel, unlike apiVersion which qml-tier ignores entirely.
    QJsonObject json = validInternalManifestJson();
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    json.remove(QStringLiteral("apiVersion"));
    json.remove(QStringLiteral("hostBuildId"));
    json[QStringLiteral("qmlApiVersion")] = QGCPluginQmlApiLevel;

    QString error;
    const PluginManifest manifest = PluginManifest::fromJson(json, &error);
    QVERIFY2(!manifest.id.isEmpty(), qPrintable(error));
    QCOMPARE(manifest.qmlApiVersion, QGCPluginQmlApiLevel);

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = QGCPluginApiVersion;
    host.qmlApiVersion = QGCPluginQmlApiLevel;
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(manifest.validateForHost(host, &reason));
    QVERIFY(reason.isEmpty());

    host.qmlApiVersion = QGCPluginQmlApiLevel + 1;
    QVERIFY(!manifest.validateForHost(host, &reason));
    QVERIFY(reason.contains(QStringLiteral("qmlApiVersion")));
}

void PluginManifestTest::_qmlApiVersionUndeclaredUnchecked_test()
{
    QJsonObject json = validInternalManifestJson();
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    json.remove(QStringLiteral("apiVersion"));
    json.remove(QStringLiteral("hostBuildId"));
    // qmlApiVersion intentionally absent

    QString error;
    const PluginManifest manifest = PluginManifest::fromJson(json, &error);
    QVERIFY2(!manifest.id.isEmpty(), qPrintable(error));
    QCOMPARE(manifest.qmlApiVersion, 0);

    HostInfo host;
    host.version = QVersionNumber::fromString(QStringLiteral("5.0"));
    host.apiVersion = QGCPluginApiVersion;
    host.qmlApiVersion = QGCPluginQmlApiLevel + 5; // deliberately different; must not matter
    host.buildId = QStringLiteral("abc1234");

    QString reason;
    QVERIFY(manifest.validateForHost(host, &reason));
    QVERIFY(reason.isEmpty());
}

void PluginManifestTest::_qmlApiVersionIgnoredOnNonQmlTier_test()
{
    // qmlApiVersion is meaningful only for tier qml; a malformed value on any other
    // tier must not break parsing of an otherwise-valid manifest.
    QJsonObject json = validInternalManifestJson();
    json[QStringLiteral("qmlApiVersion")] = QStringLiteral("not a number");

    QString error;
    const PluginManifest manifest = PluginManifest::fromJson(json, &error);
    QVERIFY2(!manifest.id.isEmpty(), qPrintable(error));
    QCOMPARE(manifest.qmlApiVersion, 0);
}

void PluginManifestTest::_qmlApiVersionZeroRejected_test()
{
    // An explicit 0 is indistinguishable from "undeclared" if silently accepted;
    // reject it outright since QGCPluginQmlApiLevel is never 0.
    QJsonObject json = validInternalManifestJson();
    json[QStringLiteral("tier")] = QStringLiteral("qml");
    json.remove(QStringLiteral("apiVersion"));
    json.remove(QStringLiteral("hostBuildId"));
    json[QStringLiteral("qmlApiVersion")] = 0;

    QString error;
    const PluginManifest manifest = PluginManifest::fromJson(json, &error);
    QVERIFY(manifest.id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("qmlApiVersion")));
}

void PluginManifestTest::_contributesMustBeObject_test()
{
    QString error;

    // Wrong-type 'contributes' must fail parsing, not silently coerce to empty
    QJsonObject arrayContributes = validInternalManifestJson();
    arrayContributes[QStringLiteral("contributes")] = QJsonArray();
    QVERIFY(PluginManifest::fromJson(arrayContributes, &error).id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("contributes")));

    error.clear();
    QJsonObject stringContributes = validInternalManifestJson();
    stringContributes[QStringLiteral("contributes")] = QStringLiteral("oops");
    QVERIFY(PluginManifest::fromJson(stringContributes, &error).id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("contributes")));

    // Absent 'contributes' stays valid (empty object)
    error.clear();
    QJsonObject noContributes = validInternalManifestJson();
    noContributes.remove(QStringLiteral("contributes"));
    const PluginManifest manifest = PluginManifest::fromJson(noContributes, &error);
    QVERIFY2(!manifest.id.isEmpty(), qPrintable(error));
    QVERIFY(manifest.contributes.isEmpty());
}

void PluginManifestTest::_metaDataEnvelope_test()
{
    QString error;
    const PluginManifest manifest = PluginManifest::fromMetaData(validMetaDataEnvelope(), QStringLiteral(QGCPluginInterface_iid), &error);
    QVERIFY2(!manifest.id.isEmpty(), qPrintable(error));
    QCOMPARE(manifest.id, QStringLiteral("org.example.qgc.example"));
    QCOMPARE(manifest.name, QStringLiteral("Example"));
    QCOMPARE(manifest.tier, PluginManifest::Tier::Internal);
}

void PluginManifestTest::_metaDataIidMismatch_test()
{
    QJsonObject envelope = validMetaDataEnvelope();
    envelope[QStringLiteral("IID")] = QStringLiteral("org.mavlink.qgroundcontrol.QGCPluginInterface");

    QString error;
    const PluginManifest manifest = PluginManifest::fromMetaData(envelope, QStringLiteral(QGCPluginInterface_iid), &error);
    QVERIFY(manifest.id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("IID")));
}

void PluginManifestTest::_metaDataMissingMetaData_test()
{
    QJsonObject envelope = validMetaDataEnvelope();
    envelope.remove(QStringLiteral("MetaData"));

    QString error;
    const PluginManifest manifest = PluginManifest::fromMetaData(envelope, QStringLiteral(QGCPluginInterface_iid), &error);
    QVERIFY(manifest.id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("FILE")));

    // Qt emits MetaData as an empty object when Q_PLUGIN_METADATA has no FILE argument
    error.clear();
    envelope[QStringLiteral("MetaData")] = QJsonObject();
    const PluginManifest emptyManifest = PluginManifest::fromMetaData(envelope, QStringLiteral(QGCPluginInterface_iid), &error);
    QVERIFY(emptyManifest.id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("FILE")));
}

void PluginManifestTest::_metaDataInvalidManifest_test()
{
    QJsonObject invalidManifest = validInternalManifestJson();
    invalidManifest.remove(QStringLiteral("id"));
    QJsonObject envelope = validMetaDataEnvelope();
    envelope[QStringLiteral("MetaData")] = invalidManifest;

    QString error;
    const PluginManifest manifest = PluginManifest::fromMetaData(envelope, QStringLiteral(QGCPluginInterface_iid), &error);
    QVERIFY(manifest.id.isEmpty());
    QVERIFY(error.contains(QStringLiteral("id")));
}

void PluginManifestTest::_hostInfo_test()
{
    const HostInfo host = QGCPluginLoader::hostInfo();
    QVERIFY(!host.version.isNull());
    QVERIFY(!host.buildId.isEmpty());
    QCOMPARE(host.apiVersion, QGCPluginApiVersion);
    QCOMPARE(host.qmlApiVersion, QGCPluginQmlApiLevel);
}

UT_REGISTER_TEST(PluginManifestTest, TestLabel::Unit)
