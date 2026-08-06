#pragma once

#include "QmlUITestBase.h"

/// Regression test for the tool-drawer force-quit trap: a custom toolbar QML
/// that fails to load must not strand the user with no way to exit the drawer.
class ToolDrawerEscapeUITest : public QmlUITestBase
{
    Q_OBJECT

public:
    ToolDrawerEscapeUITest() = default;

private slots:
    void _testBrokenToolbarSourceLeavesExitPath();
};
