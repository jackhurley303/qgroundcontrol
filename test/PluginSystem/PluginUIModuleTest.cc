/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginUIModuleTest.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtQml/QQmlComponent>

#include "UnitTestList.h"

namespace {

constexpr const char* kRosterPath = ":/qml/QGroundControl/PluginUI/qmldir";

/// The creatable C++ types PluginUIModule.cc re-registers — QGC_PLUGIN_UI_CPP_TYPES
/// in src/PluginSystem/CMakeLists.txt minus the singleton, which cannot be a
/// component root and is covered by _cppSingletonIsSharedWithHost_test instead.
/// A type published there but never registered fails here.
const QStringList kCppTypes = {
    QStringLiteral("QGCPalette"),
    QStringLiteral("PlanMasterController"),
};

struct RosterEntry
{
    QString name;
    bool singleton = false;
};

/// Reads the shipped roster. Deliberately parses the resource rather than a source
/// path: what the engine resolves is what should be checked.
QList<RosterEntry> readRoster(QString* errorOut)
{
    QFile file(QString::fromLatin1(kRosterPath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *errorOut = QStringLiteral("cannot open %1").arg(QString::fromLatin1(kRosterPath));
        return {};
    }

    QList<RosterEntry> entries;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const bool singleton = (tokens.first() == QLatin1String("singleton"));
        if (singleton) {
            tokens.removeFirst();
        }
        static const QStringList directives = {
            QStringLiteral("module"),   QStringLiteral("depends"), QStringLiteral("import"),
            QStringLiteral("typeinfo"), QStringLiteral("prefer"),  QStringLiteral("internal"),
            QStringLiteral("optional"),
        };
        if (!singleton && directives.contains(tokens.first())) {
            continue;
        }
        if (tokens.size() != 3) {
            *errorOut = QStringLiteral("unparsable roster line: %1").arg(line);
            return {};
        }
        entries.append({tokens.first(), singleton});
    }

    if (entries.isEmpty()) {
        *errorOut = QStringLiteral("roster declared no types");
    }
    return entries;
}

}  // namespace

void PluginUIModuleTest::initTestCase()
{
    UnitTest::initTestCase();

    _engine = new QQmlEngine(this);
    // What QGCCorePlugin::createQmlApplicationEngine() does for the real engine.
    // Without it the module's qmldir resource is never found.
    _engine->addImportPath(QStringLiteral("qrc:/qml"));
}

void PluginUIModuleTest::cleanupTestCase()
{
    delete _engine;
    _engine = nullptr;
    UnitTest::cleanupTestCase();
}

void PluginUIModuleTest::init()
{
    UnitTest::init();

    // Materialising a real control populates Qt's font database, which warns on
    // systems without the aliased families. Environmental, not the module's — and
    // ignoreLogMessage tolerates its absence, so this stays correct where the
    // warning never fires.
    ignoreLogMessage("qt.qpa.fonts", QtWarningMsg, QRegularExpression(QStringLiteral("font family aliases")));
}

QObject* PluginUIModuleTest::_create(const QByteArray& qml)
{
    QQmlComponent component(_engine);
    component.setData(qml, QUrl());
    if (!component.isReady()) {
        _lastError = component.errorString();
        return nullptr;
    }
    _lastError.clear();
    return component.create();
}

void PluginUIModuleTest::_compositeTypeIsSharedWithHost_test()
{
    // Copies of a .qml registered under two URIs produce two distinct types that
    // still render identically. The roster's relative paths avoid that by pointing
    // both URIs at one URL; this is the assertion that says so.
    QScopedPointer<QObject> root(_create(R"(
        import QtQuick
        import QGroundControl.Controls as Host
        import QGroundControl.PluginUI as Published

        QtObject {
            property QtObject hostLabel: Host.QGCLabel { }
            property QtObject publishedLabel: Published.QGCLabel { }

            property bool hostIsPublished: hostLabel instanceof Published.QGCLabel
            property bool publishedIsHost: publishedLabel instanceof Host.QGCLabel
        }
    )"));
    QVERIFY2(root, qPrintable(_lastError));

    QVERIFY2(root->property("hostIsPublished").toBool(),
             "a host QGCLabel is not an instance of the published QGCLabel — the module forked the type");
    QVERIFY2(root->property("publishedIsHost").toBool(),
             "a published QGCLabel is not an instance of the host QGCLabel — the module forked the type");
}

void PluginUIModuleTest::_singletonIsSharedWithHost_test()
{
    // A duplicated singleton renders correctly (both instances read the same app
    // globals) but diverges the moment either is written to.
    QScopedPointer<QObject> root(_create(R"(
        import QtQuick
        import QGroundControl.Controls as Host
        import QGroundControl.PluginUI as Published

        QtObject {
            property bool sameInstance: Host.ScreenTools === Published.ScreenTools
        }
    )"));
    QVERIFY2(root, qPrintable(_lastError));

    QVERIFY2(root->property("sameInstance").toBool(),
             "QGroundControl.PluginUI.ScreenTools is a second singleton instance, not the host's");
}

void PluginUIModuleTest::_cppSingletonIsSharedWithHost_test()
{
    // Re-registering a C++ QML_SINGLETON under a second URI gives each engine one
    // instance per URI. QGCFileDialogController's file operations are static, so the
    // split would pass every functional test — but fileImported()/importFailed() are
    // emitted from one instance only. PluginUIModule.cc delegates instead.
    QScopedPointer<QObject> root(_create(R"(
        import QtQuick
        import QGC as App
        import QGroundControl.PluginUI as Published

        QtObject {
            property bool sameInstance: App.QGCFileDialogController === Published.QGCFileDialogController
        }
    )"));
    QVERIFY2(root, qPrintable(_lastError));

    QVERIFY2(root->property("sameInstance").toBool(),
             "QGroundControl.PluginUI.QGCFileDialogController is a second instance, not the host's");
}

void PluginUIModuleTest::_cppTypesResolve_test()
{
    for (const QString& name : kCppTypes) {
        QQmlComponent component(_engine);
        component.setData(QStringLiteral("import QtQuick\n"
                                         "import QGroundControl.PluginUI\n"
                                         "%1 { }\n")
                              .arg(name)
                              .toUtf8(),
                          QUrl());
        QVERIFY2(component.isReady(), qPrintable(QStringLiteral("%1: %2").arg(name, component.errorString())));
    }
}

void PluginUIModuleTest::_everyRosterTypeResolves_test()
{
    QString error;
    const QList<RosterEntry> roster = readRoster(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // A mistyped relative path resolves to nothing and otherwise only surfaces when
    // someone else's plugin loads the panel that uses it.
    for (const RosterEntry& entry : roster) {
        const QString qml =
            entry.singleton
                ? QStringLiteral("import QtQuick\nimport QGroundControl.PluginUI\nQtObject { property var t: %1 }\n")
                      .arg(entry.name)
                : QStringLiteral("import QtQuick\nimport QGroundControl.PluginUI\n%1 { }\n").arg(entry.name);

        QQmlComponent component(_engine);
        component.setData(qml.toUtf8(), QUrl());
        QVERIFY2(component.isReady(), qPrintable(QStringLiteral("%1: %2").arg(entry.name, component.errorString())));
    }
}

void PluginUIModuleTest::_unpublishedHostTypeIsNotReachable_test()
{
    // The curation check. ToolStrip is a real, working control in
    // QGroundControl.Controls that this module deliberately does not publish —
    // a mechanism that leaked the whole directory would resolve it.
    QQmlComponent component(_engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.PluginUI

        ToolStrip { }
    )",
                      QUrl());

    QVERIFY2(component.isError(), "ToolStrip resolved through QGroundControl.PluginUI — the module is not curated");
}

void PluginUIModuleTest::_unknownTypeIsNotReachable_test()
{
    QQmlComponent component(_engine);
    component.setData(R"(
        import QtQuick
        import QGroundControl.PluginUI

        NoSuchControl { }
    )",
                      QUrl());

    QVERIFY(component.isError());
}

UT_REGISTER_TEST(PluginUIModuleTest, TestLabel::Unit)
