#pragma once

#include "UnitTest.h"

class PluginLoaderGateTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _inspectRealPluginBeforeActivation_test();
    void _activateRealPlugin_test();
    void _inspectMissingFileFails_test();
};
