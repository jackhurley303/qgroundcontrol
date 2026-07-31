import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:     _root
    height: visible ? (rowLayout.height + (_margins * 2)) : 0
    color: qgcPal.window

    property real _margins: ScreenTools.defaultFontPixelHeight / 4

    function pickLogFile() {
        if (globals.activeVehicle) {
            QGroundControl.showMessageDialog(_root, qsTr("Log Replay"), qsTr("You must close all connections prior to replaying a log."))
            return
        }

        filePicker.openForLoad()
    }

    QGCPalette { id: qgcPal }

    QGCFileDialog {
        id: filePicker
        title: qsTr("Select Telemetery Log")
        nameFilters: [ qsTr("Telemetry Logs (*.%1)").arg(_logFileExtension), qsTr("All Files (*)") ]
        folder: QGroundControl.settingsManager.appSettings.telemetrySavePath
        onAcceptedForLoad: (file) => {
            QGroundControl.linkManager.startLogReplay(file)
            close()
        }

        property string _logFileExtension: QGroundControl.settingsManager.appSettings.telemetryFileExtension
    }

    LogReplayLinkController {
        id: controller

        link: QGroundControl.linkManager.activeLogReplayLink

        onPercentCompleteChanged: (percentComplete) => slider.updatePercentComplete(percentComplete)
    }

    RowLayout {
        id: rowLayout
        anchors {
            margins: _margins
            top: parent.top
            left: parent.left
            right: parent.right
        }

        QGCButton {
            enabled: controller.link
            text: controller.isPlaying ? qsTr("Pause") : qsTr("Play")
            onClicked: controller.isPlaying = !controller.isPlaying
        }

        QGCComboBox {
            textRole: "text"
            currentIndex: 3

            model: ListModel {
                ListElement { text: "0.1";  value: 0.1 }
                ListElement { text: "0.25"; value: 0.25 }
                ListElement { text: "0.5";  value: 0.5 }
                ListElement { text: "1x";   value: 1 }
                ListElement { text: "2x";   value: 2 }
                ListElement { text: "5x";   value: 5 }
                ListElement { text: "10x";  value: 10 }
            }

            onActivated: (index) => { controller.playbackSpeed = model.get(currentIndex).value }
        }

        QGCLabel { text: controller.playheadTime }

        Slider {
            id: slider
            Layout.fillWidth: true
            from: 0
            to: 100
            enabled: controller.link

            property bool manualUpdate: false

            function updatePercentComplete(percentComplete) {
                manualUpdate = true
                value = percentComplete
                manualUpdate = false
            }

            // Seeking while the handle is held would run a full resolution pass per pixel
            // of drag, so the seek is deferred to release; a programmatic update never
            // seeks at all.
            onValueChanged: {
                if (!manualUpdate && !pressed) {
                    controller.percentComplete = value
                }
            }

            onPressedChanged: {
                if (!pressed && !manualUpdate) {
                    controller.percentComplete = value
                }
            }
        }

        QGCLabel { text: controller.totalTime }

        QGCButton {
            text: qsTr("Load Telemetry Log")
            onClicked: pickLogFile()
            visible: !controller.link
        }

        QGCButton {
            text: qsTr("Close")
            onClicked: {
                var activeVehicle = QGroundControl.multiVehicleManager.activeVehicle
                if (activeVehicle) {
                    activeVehicle.closeVehicle()
                }
                QGroundControl.settingsManager.flyViewSettings.showLogReplayStatusBar.rawValue = false
            }
        }
    }
}
