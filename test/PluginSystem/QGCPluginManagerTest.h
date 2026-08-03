#pragma once

#include "BaseClasses/TempDirectoryTest.h"

#include <QtCore/QJsonObject>

/// TempDirectoryTest: the consent-model tests (D10/D15) build real package
/// directories on disk — under a test-scoped user plugins dir for the untrusted
/// class, under a plain temp dir for the trusted (bundle-dir) class.
class QGCPluginManagerTest : public TempDirectoryTest
{
    Q_OBJECT

private slots:
    void init() override;

    void _disabledNeverActivated_test();
    void _incompatibleRecorded_test();
    void _settingsKeyedById_test();
    void _failedWithoutIdNotRegistered_test();
    void _duplicateIdFails_test();
    void _setPluginEnabledPersistsAndReconciles_test();
    void _reloadUnknownId_test();
    void _reloadKeepsIdentityOnFailedInspect_test();
    void _knownPluginsReflectsRecords_test();
    void _contributionsAddedAndRemoved_test();
    void _loggingControllerFromManifest_test();
    void _notifyEpilogueEmitsOnce_test();
    void _notifyEpilogueOwnsReplayExtension_test();

    void _userDirPluginNeedsApprovalFirstSight_test();
    void _approvalActivatesAndPersists_test();
    void _changedContentReprompts_test();
    void _bundleDirPluginTrusted_test();

    void _crashSentinelQuarantines_test();
    void _crashSentinelOutranksConsent_test();
    void _sentinelClearedAfterActivation_test();
#if defined(Q_OS_MACOS)
    void _quarantinedBinaryGated_test();
#endif

private:
    // Writes a package directory <parentDir>/<id>/ with a valid manifest (tier "qml",
    // or "sdk" with a placeholder bin/ binary); returns the package dir path.
    QString _writePackage(const QString& parentDir, const QString& id, const QString& tier,
                          const QString& description = QStringLiteral("Manager fixture"));
};
