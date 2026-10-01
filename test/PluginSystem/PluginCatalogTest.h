#pragma once

#include "UnitTest.h"

class PluginCatalogTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _parseValidIndex_test();
    void _unknownFieldsAndPackageKeysIgnored_test();
    void _authorFillsManifestVendor_test();
    void _schemaVersionNewerRefused_test();
    void _schemaVersionMissingOrInvalidRefused_test();
    void _malformedData_test();
    void _missingSha256Refused_test();
    void _badSha256Refused_test();
    void _missingUrlOrSizeRefused_test();
    void _unparseableVersionRefused_test();
    void _versionSuffixRefused_test();
    void _anyPackageOnSdkTierRefused_test();
    void _anyPackageOnQmlTierAccepted_test();
    void _internalTierRefused_test();
    void _sdkMissingApiVersionRefused_test();
    void _duplicateIdsRefused_test();
    void _hostVersionOutOfRangeHidden_test();
    void _apiVersionMismatchHidden_test();
    void _noPackageForPlatformHidden_test();
    void _newestCompatibleSkipsIncompatibleNewer_test();
    void _newestIsNumericNotLexical_test();
    void _platformPackagePreferredOverAny_test();
    void _updateForComparesInstalled_test();
    void _find_test();
    void _nonAsciiDigitSha256Refused_test();
    void _hugeNumbersRefused_test();
};
