#pragma once

#include "BaseClasses/TempDirectoryTest.h"

/// TempDirectoryTest: mirrors QGCPluginManagerTest's setup so the crash-sentinel and
/// consent-digest QSettings keys start clean for every test.
class PluginRecordStoreTest : public TempDirectoryTest
{
    Q_OBJECT

private slots:
    void init() override;

    void _findReturnsMatchingRecordById_test();
    void _findReturnsFirstOfDuplicateIds_test();
    void _findReturnsNullptrWhenAbsent_test();
    void _removeIfDropsMatchingRecords_test();
    void _clearDropsRecordsButNotPersistedSentinel_test();

    void _checkCrashSentinelPromotesLingeringId_test();
    void _checkCrashSentinelNoOpWithoutLingeringId_test();
    void _clearCrashQuarantineClearsPersistedAndInMemory_test();
    void _armLoadingSentinelWritesThenClearsOnScopeExit_test();

    void _consentDigestRoundTrip_test();
    void _registerAndIsPluginEnabled_test();
};
