#pragma once

#include "UnitTest.h"

class PluginContributionsTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _fullContributes_test();
    void _emptyContributes_test();
    void _panelDefaults_test();
    void _missingToolMenuRequired_test();
    void _missingPanelRequired_test();
    void _wrongTypes_test();
    void _badDefaultPosition_test();
    void _unknownKeysIgnored_test();
    void _flags_test();
    void _contributesQml_test();
    void _relativeUrlsResolvedPackageRelative_test();
    void _qrcAndHostResourceUrlsPassThrough_test();
    void _relativeUrlWithoutPackageContextPassesThrough_test();
    void _absoluteSchemeUrlsPassThroughEvenInPackageContext_test();
};
