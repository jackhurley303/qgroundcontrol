/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "UnitTest.h"

/// Proves the *connection* half of QGCPlugin::cleanup()'s contract against the
/// reference plugin: a real ExamplePlugin dylib is activated, init()ed,
/// cleanup()ed and activated again, and the host service's connection count must
/// come back to where it started.
///
/// Scope, stated because the gap matters: this is a sender-side census, so it sees
/// only what the plugin connected *to a host object*. The contract's other clause —
/// no QObject the plugin created outlives cleanup() — is not measured here. An
/// object leaked with no host connection (a self-contained QTimer, say) passes this
/// test. Catching that needs a process-wide object census, which costs a callback on
/// every QObject construction and is out of proportion to a leak that cannot become
/// a crash while the plugin image stays mapped.
///
/// Loader-level rather than manager-level on purpose — the manager owns its host
/// services privately, and the census has to be taken on the object the plugin
/// actually connects to.
class PluginTeardownTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _reactivationDoesNotDuplicateConnections_test();
    void _teardownCensusIsUnaffectedByLiveQml_test();
};
