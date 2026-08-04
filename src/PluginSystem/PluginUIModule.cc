/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginUIModule.h"

#include <QtQml/QQmlEngine>
#include <QtQml/qqml.h>

#include "PlanMasterController.h"
#include "PluginUIGlobal.h"
#include "QGCFileDialogController.h"
#include "QGCPalette.h"

namespace {

constexpr const char* kUri = "QGroundControl.PluginUI";
constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 0;

}  // namespace

void PluginUIModule::registerTypes()
{
    // Registration is process-wide, but the app and the unit-test boot share an
    // entry point, so guard against a second pass rather than let Qt warn about
    // duplicate type names.
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    qmlRegisterType<QGCPalette>(kUri, kVersionMajor, kVersionMinor, "QGCPalette");
    qmlRegisterType<PlanMasterController>(kUri, kVersionMajor, kVersionMinor, "PlanMasterController");

    // Delegate to the QGC-URI instance instead of registering a second one.
    // QGCFileDialogController is a QML_SINGLETON, and a plain re-registration gives
    // each engine one instance per URI. Its filesystem operations are static, so the
    // split would look correct in every test that calls one — but fileImported() and
    // importFailed() are emitted from a single instance, and a panel that imported
    // through the other URI would silently never hear them.
    qmlRegisterSingletonType<QGCFileDialogController>(
        kUri, kVersionMajor, kVersionMinor, "QGCFileDialogController", [](QQmlEngine* engine, QJSEngine*) -> QObject* {
            return engine->singletonInstance<QGCFileDialogController*>(QStringLiteral("QGC"),
                                                                       QStringLiteral("QGCFileDialogController"));
        });

    // The host-global facade. QGroundControlQmlGlobal itself is not published —
    // only this narrow view over it. Registered under its own class name, like
    // every other C++ type here: tools/derive_plugin_ui_sdk.py carves the SDK's
    // .qmltypes by class name from this module's auto-registration under QGC
    // (needed only to get an exports: line — see PluginUIGlobal.h), so the name
    // used here must match it exactly or the SDK-side type would be unreachable
    // under the name a plugin actually imports. create() forwards to whatever
    // the engine already resolves as the real QGC-URI singleton rather than
    // holding a second copy of its state.
    qmlRegisterSingletonType<QGCPluginUIGlobal>(
        kUri, kVersionMajor, kVersionMinor, "QGCPluginUIGlobal", [](QQmlEngine* engine, QJSEngine* jsEngine) -> QObject* {
            return QGCPluginUIGlobal::create(engine, jsEngine);
        });
}
