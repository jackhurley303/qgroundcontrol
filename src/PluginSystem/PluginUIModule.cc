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
}
