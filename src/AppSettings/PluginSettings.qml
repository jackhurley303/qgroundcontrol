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
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

SettingsPage {
    id: root

    property var _pluginSettings: QGroundControl.settingsManager.pluginSettings

    // Runtime plugin unload/reload supported on desktop platforms
    // Note: Plugin code changes still require rebuilding the app
    readonly property bool _supportsRuntimeReload: Qt.platform.os === "osx" ||
                                                     Qt.platform.os === "linux" ||
                                                     Qt.platform.os === "windows"

    QGCPalette { id: qgcPal }

    QGCFileDialog {
        id:             installDialog
        title:          qsTr("Select plugin package")
        selectFolder:   false
        nameFilters:    ["QGC Plugin Packages (*.qgcplugin)"]

        onAcceptedForLoad: (file) => {
            const error = QGroundControl.pluginManager.installPlugin(file)
            if (error.length > 0) {
                installErrorDialog.text = error
                installErrorDialog.open()
            }
        }
    }

    MessageDialog {
        id:         installErrorDialog
        title:      qsTr("Plugin install failed")
        buttons:    MessageDialog.Ok
    }

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

        QGCLabel {
            id:                 crashBanner
            Layout.fillWidth:   true
            property var _crashedPlugins: QGroundControl.pluginManager.knownPlugins.filter(p => p.state === "Quarantined")
            visible:            _crashedPlugins.length > 0
            text:               qsTr("QGC crashed while loading %1 last run — re-enable to retry.").arg(_crashedPlugins.map(p => p.name).join(", "))
            wrapMode:           Text.WordWrap
            color:              qgcPal.colorRed
        }

        QGCButton {
            Layout.alignment:   Qt.AlignRight
            text:               qsTr("Install plugin…")
            visible:            _supportsRuntimeReload
            onClicked:          installDialog.openForLoad()
        }

        Repeater {
            model: QGroundControl.pluginManager.knownPlugins

            RowLayout {
                Layout.fillWidth:   true

                ColumnLayout {
                    Layout.fillWidth:   true
                    spacing:            0

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               modelData.name + " " + modelData.version + " — " + modelData.vendor
                        wrapMode:           Text.WordWrap
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               modelData.tier + " — " + modelData.description
                        wrapMode:           Text.WordWrap
                        font.pointSize:     ScreenTools.smallFontPointSize
                        visible:            modelData.description.length > 0
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               modelData.statusText
                        wrapMode:           Text.WordWrap
                        font.pointSize:     ScreenTools.smallFontPointSize
                        color: {
                            switch (modelData.state) {
                            case "Active":
                                return qgcPal.colorGreen
                            case "Incompatible":
                            case "Failed":
                            case "Quarantined":
                                return qgcPal.colorRed
                            case "NeedsApproval":
                                return qgcPal.colorOrange
                            default:
                                return qgcPal.text
                            }
                        }
                    }
                }

                QGCButton {
                    text:       qsTr("Enable")
                    visible:    modelData.state === "NeedsApproval"
                    onClicked:  QGroundControl.pluginManager.approvePlugin(modelData.id)
                }

                FactCheckBoxSlider {
                    id:                 pluginEnabledSlider
                    fact:               _pluginSettings.pluginEnabledFact(modelData.id)
                    visible:            fact !== null && modelData.state !== "NeedsApproval"

                    Connections {
                        target: pluginEnabledSlider.fact
                        enabled: _supportsRuntimeReload  // Only hook up reload on supported platforms

                        function onValueChanged() {
                            QGroundControl.pluginManager.setPluginEnabled(modelData.id, pluginEnabledSlider.fact.value)
                        }
                    }
                }

                QGCButton {
                    text:       qsTr("Remove")
                    visible:    modelData.removable && _supportsRuntimeReload
                    onClicked:  QGroundControl.showMessageDialog(
                                    root,
                                    qsTr("Remove Plugin"),
                                    qsTr("Are you sure you want to remove '%1'? This deletes it from disk.").arg(modelData.name),
                                    Dialog.Ok | Dialog.Cancel,
                                    function () {
                                        const error = QGroundControl.pluginManager.removePlugin(modelData.id)
                                        if (error.length > 0) {
                                            installErrorDialog.text = error
                                            installErrorDialog.open()
                                        }
                                    })
                }
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            QGroundControl.pluginManager.knownPlugins.length === 0
            text:               qsTr("No plugins found.")
            wrapMode:           Text.WordWrap
            font.italic:        true
        }
    }
}
