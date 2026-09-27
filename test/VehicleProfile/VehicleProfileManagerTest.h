#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include "UnitTest.h"

/// Tests for VehicleProfileManager: the folder of `.vehicle` files, create, save, delete, and
/// reload after a change made outside QGC.
class VehicleProfileManagerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void initTestCase() override;

    void _createNamesFileFromName_data();
    void _createNamesFileFromName();
    void _createAddsSuffixOnNameClash();
    void _createKeepsOtherUnicodeFormApart();
    void _editSavesToOwnFile();
    void _renameKeepsFileName();
    void _deleteRemovesFile();
    void _ownSaveIsNotAnOutsideChange();
    void _brokenFileSkippedWithWarning();
    void _outsideAddAppears();
    void _outsideEditReloadsSameEntry();
    void _outsideDeleteRemoves();
    void _duplicateIdSkipped();
    void _idChangedOnDiskReplacesEntry();
    void _followsSavePathChange();

private:
    /// A valid version 1 `.vehicle` file with this id and name.
    static QByteArray _vehicleJson(const QString& id, const QString& name);
    /// Writes `bytes` in place, the way an outside tool that does not write atomically would.
    static bool _writeInPlace(const QString& filePath, const QByteArray& bytes);

    /// Set by initTestCase(): whether this machine delivers file change notifications at all.
    bool _watcherNotificationsDelivered = false;
};
