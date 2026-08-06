#include "ToolDrawerEscapeUITest.h"

#include <QtCore/QRegularExpression>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

UT_REGISTER_TEST(ToolDrawerEscapeUITest, TestLabel::Integration)

// Regression test for the tool-drawer force-quit trap (plugin-activation-qml-cache.md U2):
// a custom toolbar QML that fails to load must not strand the user with no exit path.
// Before the fix, declaring a non-empty toolbarSource unconditionally hid the default
// toolbar's QGC-logo exit button, and a Loader that fails to resolve never emits
// exitRequested — leaving a full-screen input-eating drawer with no way out.

void ToolDrawerEscapeUITest::_testBrokenToolbarSourceLeavesExitPath()
{
    startUI();
    if (QTest::currentTestFailed()) return;

    QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("toolbar_qgcLogo")), "Q logo button not found before opening drawer");

    // Deliberately broken toolbarSource: this file does not exist. The Loader
    // logs this through the QML engine's default category, not a QGC qCWarning
    // category — expect it so the strict log-message gate doesn't fail the test
    // on the very error condition it exists to reproduce.
    expectLogMessage("default", QtWarningMsg, QRegularExpression("ToolDrawerEscapeUITest_DoesNotExist\\.qml"));

    bool invoked = QMetaObject::invokeMethod(
        _window, "showTool",
        Q_ARG(QVariant, QStringLiteral("Broken Toolbar Test")),
        Q_ARG(QVariant, QStringLiteral("qrc:/qml/QGroundControl/AnalyzeView/AnalyzeView.qml")),
        Q_ARG(QVariant, QString()),
        Q_ARG(QVariant, QStringLiteral("qrc:/qml/ToolDrawerEscapeUITest_DoesNotExist.qml")));
    QVERIFY2(invoked, "Failed to invoke showTool() on the MainWindow root object");

    QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("mainView_toolDrawer")), "Tool drawer did not open");

    // Confirms the toolbar genuinely failed to load rather than the assertions
    // below passing for an unrelated reason (e.g. the file happening to resolve).
    verifyExpectedLogMessage();

    // The default toolbar's QGC-logo button must reappear once the custom toolbar
    // fails to load — this is the fix under test.
    QQuickItem *qgcLogo = findVisibleItem(_rootItem, QStringLiteral("toolbar_qgcLogo"));
    QVERIFY2(qgcLogo, "QGC-logo exit button did not reappear after custom toolbar failed to load");

    // Clicking the logo opens the view-select dropdown; select Fly to close the drawer.
    QVERIFY2(clickToolSelectDropdownButton(QStringLiteral("toolbar_viewFly")), "Failed to select Fly view from dropdown");

    QVERIFY2(!findVisibleItem(_rootItem, QStringLiteral("mainView_toolDrawer"), 1000), "Tool drawer still visible after exiting via QGC-logo button");

    stopUI();
}
