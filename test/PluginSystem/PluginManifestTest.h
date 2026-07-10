#pragma once

#include "UnitTest.h"

class PluginManifestTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _malformedJson_test();
    void _missingRequiredFields_test();
    void _badTier_test();
    void _hostVersionMinEqualsHost_test();
    void _hostVersionMaxEqualsHostRejected_test();
    void _hostVersionEmptyMaxUnbounded_test();
    void _hostVersionBelowMinRejected_test();
    void _nullHostVersionSkipsRangeCheck_test();
    void _internalTierHashMatch_test();
    void _internalTierHashMismatch_test();
    void _internalTierMissingHashInvalid_test();
    void _apiVersionMismatch_test();
    void _metaDataEnvelope_test();
    void _metaDataIidMismatch_test();
    void _metaDataMissingMetaData_test();
    void _metaDataInvalidManifest_test();
    void _hostInfo_test();
};
