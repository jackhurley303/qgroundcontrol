/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

SettingsPage {
    property var _pluginSettings: QGroundControl.settingsManager.pluginSettings

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Plugins")

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Control which plugins are visible in the Tools Menu.")
            wrapMode:           Text.WordWrap
        }

        Repeater {
            model: QGroundControl.pluginManager.loadedPlugins

            FactCheckBoxSlider {
                Layout.fillWidth:   true
                text:               modelData.name + qsTr(" Plugin")
                fact:               _pluginSettings.pluginEnabledFact(modelData.name)
                visible:            fact !== null
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            QGroundControl.pluginManager.loadedPlugins.length === 0
            text:               qsTr("No plugins are currently loaded.")
            wrapMode:           Text.WordWrap
            font.italic:        true
        }
    }
}
