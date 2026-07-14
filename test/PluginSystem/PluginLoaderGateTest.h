#pragma once

#include "BaseClasses/TempDirectoryTest.h"

/// TempDirectoryTest, not UnitTest: package-discovery tests build real qgcplugin.json
/// + bin/ layouts on disk (S4's recipe, recreated per U3.1 — the spike harness was
/// throwaway and deleted).
class PluginLoaderGateTest : public TempDirectoryTest
{
    Q_OBJECT

private slots:
    void _inspectRealPluginBeforeActivation_test();
    void _activateRealPlugin_test();
    void _inspectMissingFileFails_test();

    void _inspectQmlPackageActivatesWithoutBinary_test();
    void _inspectSdkPackageLoadsBinary_test();
    void _inspectPackageBadLayoutErrorsLegible_test();
    void _inspectQmlPackageRejectsBinary_test();
    void _inspectQmlPackageRejectsReplayDeclaration_test();
    void _inspectPackageRejectsInternalTier_test();
    void _inspectDirectoriesDiscoversPackagesAlongsideBareDylibs_test();

private:
    QString _writePackage(const QString& subdirName, const QJsonObject& manifestJson);
};
