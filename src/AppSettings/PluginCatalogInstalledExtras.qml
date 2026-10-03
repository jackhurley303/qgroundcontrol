/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// What the catalog adds under one row of the Installed tab: an "Update available" badge, or the
/// restart line once the update is staged. `plugin` is the row's QGCPluginManager.knownPlugins
/// entry; `installer`, the PluginCatalogInstaller, finds the catalog entry by its id. The Update
/// button is PluginCatalogUpdateButton, which sits in line with the row's other controls.
ColumnLayout {
    id: root

    required property var plugin
    required property var installer

    // A plugin the catalog does not list has no entry, and no badge.
    readonly property var _entry: installer.entryFor(plugin.id)
    readonly property bool _updateAvailable:    _entry !== null && _entry.updateAvailable
    readonly property bool _updateStaged:       plugin.updateStaged === true

    Layout.fillWidth:   true
    spacing:            0

    QGCPalette { id: qgcPal }

    QGCLabel {
        objectName:         "pluginUpdateBadge_" + root.plugin.id
        Layout.fillWidth:   true
        visible:            root._updateAvailable && !root._updateStaged
        text:               root._updateAvailable ? qsTr("Update available: %1 → %2").arg(root._entry.installedVersion).arg(root._entry.version) : ""
        wrapMode:           Text.Wrap
        font.pointSize:     ScreenTools.smallFontPointSize
        color:              qgcPal.colorOrange
    }

    QGCLabel {
        objectName:         "pluginRestartLine_" + root.plugin.id
        Layout.fillWidth:   true
        visible:            root._updateStaged
        text:               qsTr("Restart QGroundControl to finish the update")
        wrapMode:           Text.Wrap
        font.pointSize:     ScreenTools.smallFontPointSize
        color:              qgcPal.colorOrange
    }
}
