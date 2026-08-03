#pragma once

#include "BaseClasses/TempDirectoryTest.h"

/// TempDirectoryTest: consent-digest tests need real files on disk (a bare dylib
/// fixture and a bundle-dir fixture), mirroring QGCPluginManagerTest's setup.
class PluginTrustGateTest : public TempDirectoryTest
{
    Q_OBJECT

private slots:
    void init() override;

    void _isUserDirPluginTrueForUserDirContainer_test();
    void _isUserDirPluginFalseForOtherContainer_test();
    void _isUserDirPluginFalseWhenUserPluginsDirEmpty_test();

    void _consentDigestStableForUnchangedContent_test();
    void _consentDigestChangesWithContent_test();
    void _consentDigestEmptyOnUnreadableFile_test();

    void _applyTrustGateSkipsNonDiscoveredRecord_test();
    void _applyTrustGateCrashSentinelOutranksConsent_test();
    void _applyTrustGateBundleDirTrustedOnFirstSight_test();
    void _applyTrustGateUserDirNeedsApprovalFirstSight_test();
    void _applyTrustGateUserDirApprovedDigestPasses_test();
    void _applyTrustGateUserDirChangedContentReprompts_test();
};
