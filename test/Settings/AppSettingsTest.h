#pragma once

#include "UnitTest.h"

class Fact;

class AppSettingsTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _preferredFirmwareClassEnumFiltered();
    void _offlineEditingFirmwareClassEnumFiltered();
    void _vehicleSaveFolderExistsOnStart();
    void _vehicleSaveFolderCreatedAfterSavePathChange();

private:
    void _verifyFirmwareClassEnumFiltered(Fact *fact);
};
