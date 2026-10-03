/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls

/// The Update button of one Installed row, shown while the catalog offers a newer version that
/// is not staged yet. `plugin` is the row's QGCPluginManager.knownPlugins entry; `installer`,
/// the PluginCatalogInstaller, finds the catalog entry and owns the consent dialog.
QGCButton {
    id: root

    required property var plugin
    required property var installer

    readonly property var _entry: installer.entryFor(plugin.id)

    objectName: "pluginUpdateButton_" + plugin.id
    text:       qsTr("Update")
    visible:    _entry !== null && _entry.updateAvailable && plugin.updateStaged !== true
    enabled:    PluginCatalogManager.installingId === ""
    onClicked:  installer.requestInstall(_entry)
}
