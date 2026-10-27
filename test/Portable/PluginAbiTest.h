#pragma once

#include "PortableTest.h"

/// The plugin SDK's ABI watchdog. A plugin built against a packaged SDK must pass today's
/// QGCPluginLoader gate. CI points QGC_TEMPLATE_PLUGIN_PATH at the verifier's kept example
/// plugin; with the variable unset the test skips.
class PluginAbiTest : public PortableTest
{
    Q_OBJECT

private slots:
    void _inspectAndActivateTemplatePlugin_test();
};
