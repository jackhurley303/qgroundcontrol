#pragma once

#include "UnitTest.h"

class QGCPluginManagerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _disabledNeverActivated_test();
    void _defaultDisabledById_test();
    void _incompatibleRecorded_test();
    void _settingsKeyedById_test();
    void _failedWithoutIdNotRegistered_test();
    void _duplicateIdFails_test();
    void _setPluginEnabledPersistsAndReconciles_test();
    void _reloadUnknownId_test();
    void _reloadKeepsIdentityOnFailedInspect_test();
    void _knownPluginsReflectsRecords_test();
};
