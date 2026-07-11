#pragma once

#include "UnitTest.h"

class HostServicesTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _registryLookup_test();
    void _registryDuplicateFirstWins_test();
    void _defaultServicesResolveAndCast_test();
    void _telemetryLoggingDelegates_test();
    void _telemetryLoggingRelaysSignals_test();
    void _replayInactiveNoOps_test();
    void _replayRejectsEmptyPath_test();
    void _replayRegistriesSmoke_test();
};
