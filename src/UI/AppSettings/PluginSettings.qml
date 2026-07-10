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
    
    // Runtime plugin unload/reload supported on desktop platforms
    // Note: Plugin code changes still require rebuilding the app
    readonly property bool _supportsRuntimeReload: Qt.platform.os === "osx" || 
                                                     Qt.platform.os === "linux" ||
                                                     Qt.platform.os === "windows"

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Plugins")

        QGCLabel {
            Layout.fillWidth:   true
            text:               _supportsRuntimeReload ? 
                                qsTr("Enable or disable plugins. Disabling a plugin will unload it from memory. Plugin code changes require rebuilding the application.") :
                                qsTr("Enable or disable plugins. Toggle settings to control which plugins load at startup, but changing which plugins are included requires rebuilding the APK.")
            wrapMode:           Text.WordWrap
        }

        Repeater {
            model: _pluginSettings.registeredPluginIds

            FactCheckBoxSlider {
                Layout.fillWidth:   true
                text:               (fact ? fact.label : modelData) + qsTr(" Plugin")
                fact:               _pluginSettings.pluginEnabledFact(modelData)
                visible:            fact !== null

                Connections {
                    target: fact
                    enabled: _supportsRuntimeReload  // Only hook up reload on supported platforms

                    function onValueChanged() {
                        QGroundControl.pluginManager.setPluginEnabled(modelData, fact.value)
                    }
                }
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
