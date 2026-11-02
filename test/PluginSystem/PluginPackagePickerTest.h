#pragma once

#include "BaseClasses/TempDirectoryTest.h"

/// Drives PluginPackagePicker::installPickedFile() directly, standing in for the Android
/// picker by writing the "picked copy" into the staging folder itself.
class PluginPackagePickerTest : public TempDirectoryTest
{
    Q_OBJECT

private slots:
    void init() override;

    void _goodPackageInstallsAndCopyDeleted_test();
    void _badPackageFailsAndCopyDeleted_test();
    void _failedCopyInstallsNothingAndCopyDeleted_test();
    void _emptyPathInstallsNothing_test();

private:
    // Writes bytes to <stagingDir>/<fileName>, as the picker's copy would; returns the path.
    static QString _writeStagedCopy(const QString& fileName, const QByteArray& bytes);
};
