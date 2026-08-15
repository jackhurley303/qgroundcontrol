/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginTeardownTest.h"

#include <QtCore/QMetaMethod>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopedPointer>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

#include "HostServices/QGCAppServiceImpl.h"
#include "HostServices/QGCHostServicesImpl.h"
#include "QGCAppService.h"
#include "QGCPlugin.h"
#include "QGCPluginLoader.h"

namespace {

// Path to the real ExamplePlugin dylib, injected by test/PluginSystem/CMakeLists.txt
// as a compile definition. The reference plugin is the subject deliberately: the
// contract it demonstrates is the one SDK-README.md tells plugin authors to follow.
// Empty only in a tree built without plugins/ at all — a shape the root CMakeLists.txt
// supports, and the one case where there is no reference plugin to hold to the contract.
#ifndef QGC_EXAMPLE_PLUGIN_PATH
#define QGC_EXAMPLE_PLUGIN_PATH ""
#endif

QString examplePluginPath()
{
    return QStringLiteral(QGC_EXAMPLE_PLUGIN_PATH);
}

/// Wraps a host-side object so a test can read its connection count. QObject::receivers()
/// is protected, so only a subclass can take the census — and it has to be taken on the
/// service object a plugin connects to, not on the registry that hands it out, which is why
/// this is a test-local template rather than an accessor added to QGCHostServicesImpl.
///
/// The census is deliberately scoped to the host's own objects: that makes it immune to
/// anything the QML engine creates (a panel never receives the services handle), and it is
/// the reading QGCPlugin::cleanup()'s contract is written against.
///
/// The absolute number is not meaningful — Qt maps a cloned signal (a default-argument
/// overload) back to its original, so some connections are counted under more than one
/// signature. Only differences from the baseline are.
template <class T>
class ConnectionCensus : public T
{
public:
    using T::T;

    int connectionCount() const
    {
        int total = 0;
        const QMetaObject* metaObject = this->metaObject();
        for (int i = 0; i < metaObject->methodCount(); ++i) {
            const QMetaMethod method = metaObject->method(i);
            if (method.methodType() != QMetaMethod::Signal) {
                continue;
            }
            // receivers() takes a SIGNAL()-encoded signature: the macro's leading '2'.
            const QByteArray signature = QByteArrayLiteral("2") + method.methodSignature();
            total += this->receivers(signature.constData());
        }
        return total;
    }
};

}  // namespace

// Nothing the plugin wires up in init() may survive cleanup(), because the host hands
// the *same* host services objects to the next activation — so a survivor is duplicated
// wiring on re-enable, not a one-off leak.
void PluginTeardownTest::_reactivationDoesNotDuplicateConnections_test()
{
    if (examplePluginPath().isEmpty()) {
        QSKIP("built without plugins/example; no reference plugin to hold to the contract");
    }

    // Assembled the way QGCPluginManager::_ensureHostServices() assembles it, with the
    // one service the reference plugin consumes. Only the service is censused: the
    // registry declares no signals of its own, so a census on it could never fail.
    QGCHostServicesImpl services;
    auto* appService = new ConnectionCensus<QGCAppServiceImpl>(&services);
    services.registerService(QGCAppServiceId, appService);

    const int baselineService = appService->connectionCount();

    PluginLoadInfo info = QGCPluginLoader::inspect(examplePluginPath());
    QCOMPARE(info.state, PluginState::Discovered);
    QCOMPARE(info.manifest.id, QStringLiteral("org.qgroundcontrol.example"));

    int firstCycleConnections = 0;
    for (int cycle = 1; cycle <= 2; ++cycle) {
        QGCPluginLoader::activate(info);
        QCOMPARE(info.state, PluginState::Active);
        QVERIFY(info.plugin != nullptr);

        // Owned here so an assertion failure below still tears the instance down.
        QScopedPointer<QGCPlugin> plugin(info.plugin);
        info.plugin = nullptr;

        plugin->init(&services);

        // Positive control. Without it every assertion below would hold just as well
        // for a plugin that connected nothing at all, and the test would report a
        // clean teardown of an empty one.
        const int active = appService->connectionCount();
        QVERIFY2(active > baselineService,
                 "the reference plugin connected nothing to the host services: the census "
                 "below cannot distinguish a correct cleanup() from a missing one");

        if (cycle == 1) {
            firstCycleConnections = active;
        } else {
            QCOMPARE(active, firstCycleConnections);
        }

        // Read before the delete, matching QGCPluginManager::_deactivateRecord(): the
        // contract is that cleanup() itself restores the count, not that destroying the
        // plugin eventually does.
        plugin->cleanup();
        QCOMPARE(appService->connectionCount(), baselineService);

        info.state = PluginState::Disabled;
    }
}

// The census must stay readable in the presence of a live QML engine holding the
// plugin's own panel — the state a real disable happens in. Measured rather than
// assumed: engine-owned objects cannot reach the host services (a panel is never
// handed the services object), so they cannot move this count.
void PluginTeardownTest::_teardownCensusIsUnaffectedByLiveQml_test()
{
    if (examplePluginPath().isEmpty()) {
        QSKIP("built without plugins/example; no reference plugin to hold to the contract");
    }

    QQmlEngine engine;
    // What QGCCorePlugin::createQmlApplicationEngine() does for the real engine;
    // without it QGroundControl.PluginUI never resolves.
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    // Materialising a real control populates Qt's font database, which warns on systems
    // without the aliased families. Environmental; ignoreLogMessage tolerates its absence.
    ignoreLogMessage("qt.qpa.fonts", QtWarningMsg, QRegularExpression(QStringLiteral("font family aliases")));

    QGCHostServicesImpl services;
    auto* appService = new ConnectionCensus<QGCAppServiceImpl>(&services);
    services.registerService(QGCAppServiceId, appService);
    const int baselineService = appService->connectionCount();

    PluginLoadInfo info = QGCPluginLoader::inspect(examplePluginPath());
    QCOMPARE(info.state, PluginState::Discovered);
    QGCPluginLoader::activate(info);
    QCOMPARE(info.state, PluginState::Active);
    QVERIFY(info.plugin != nullptr);
    QScopedPointer<QGCPlugin> plugin(info.plugin);
    info.plugin = nullptr;

    plugin->init(&services);
    QVERIFY(appService->connectionCount() > baselineService);

    // What QGCPluginManager::_invalidateQmlCache() does after every activation, kept so
    // the engine is driven the way the host drives it. Inert in this process — the other
    // slot already mapped the dylib before this engine was constructed — so it is not
    // what makes the component below resolve.
    engine.clearComponentCache();

    // The plugin's QML arrives with its library (Q_INIT_RESOURCE in the factory), so
    // this url only resolves once the dylib is mapped.
    const QString panelUrl = info.contributions.flyViewPanelItem[QStringLiteral("panelUrl")].toString();
    QVERIFY(!panelUrl.isEmpty());
    QQmlComponent panel(&engine, QUrl(panelUrl));
    QVERIFY2(panel.isReady(), qPrintable(panel.errorString()));
    QScopedPointer<QObject> panelObject(panel.create());
    QVERIFY2(!panelObject.isNull(), qPrintable(panel.errorString()));

    // The panel outlives the teardown, as a popped-out one would. It holds no handle on
    // the plugin — the shipped example's panels are pure QML — so this measures the
    // narrower claim it can actually support: a live engine with the plugin's own QML
    // instantiated in it does not move the census.
    plugin->cleanup();
    QCOMPARE(appService->connectionCount(), baselineService);
}

UT_REGISTER_TEST(PluginTeardownTest, TestLabel::Unit)
