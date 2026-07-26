import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:     _root
    height: visible ? (Math.max(rowLayout.implicitHeight, loadingRow.implicitHeight) + (_margins * 2)) : 0
    color: qgcPal.window

    property real _margins: ScreenTools.defaultFontPixelHeight / 4

    // Plugin-agnostic accessor for the replay extension (null when no plugin is loaded)
    readonly property var  _replay:          QGroundControl.pluginManager.replayExtension
    readonly property bool _isLoadingRemote: _replay !== null && _replay.isLoadingRemote

    function pickLogFile() {
        if (globals.activeVehicle) {
            QGroundControl.showMessageDialog(_root, qsTr("Log Replay"), qsTr("You must close all connections prior to replaying a log."))
            return
        }

        filePicker.openForLoad()
    }

    function loadLogFile(filePath) {
        if (globals.activeVehicle) {
            QGroundControl.showMessageDialog(_root, qsTr("Log Replay"), qsTr("You must close all connections prior to replaying a log."))
            return
        }
        QGroundControl.linkManager.startLogReplay(filePath)
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

    // Loading overlay shown while a remote flight's tlog is downloading.
    RowLayout {
        id:      loadingRow
        visible: _isLoadingRemote
        anchors {
            margins: _margins
            top:     parent.top
            left:    parent.left
            right:   parent.right
        }
        spacing: ScreenTools.defaultFontPixelWidth

        QGCLabel {
            text: qsTr("Downloading flight… %1%").arg(_replay ? _replay.remoteLoadProgress : 0)
            font.bold: true
        }

        ProgressBar {
            Layout.fillWidth: true
            from:  0
            to:    100
            value: _replay ? _replay.remoteLoadProgress : 0
        }

        QGCButton {
            text:      qsTr("Cancel")
            onClicked: if (_replay) _replay.cancelRemoteLoad()
        }
    }

    RowLayout {
        id: rowLayout
        visible: !_isLoadingRemote
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

            onActivated: (index) => {
                controller.playbackSpeed = model.get(currentIndex).value
                if (_replay && _replay.isActive)
                    _replay.setPlaybackSpeed(model.get(currentIndex).value)
            }
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

            onValueChanged: {
                if (!manualUpdate && !pressed) {
                    controller.percentComplete = value
                    if (_replay && _replay.isActive)
                        _replay.seekTo(value)
                }
            }

            onPressedChanged: {
                if (!pressed && !manualUpdate) {
                    controller.percentComplete = value
                    if (_replay && _replay.isActive)
                        _replay.seekTo(value)
                }
            }
        }

        QGCLabel { text: controller.totalTime }

        // ── Video camera icon — visible when a video attachment is active ──────
        Item {
            visible:          _replay ? _replay.hasVideo : false
            width:            ScreenTools.defaultFontPixelHeight * 1.6
            height:           ScreenTools.defaultFontPixelHeight * 1.6
            Layout.alignment: Qt.AlignVCenter

            readonly property bool _enabled: _replay !== null && _replay.videoDurationMs > 0

            QGCColoredImage {
                anchors.fill:     parent
                source:           "/qmlimages/camera_video.svg"
                color:            videoOffsetPopover.visible ? qgcPal.brandingBlue
                                      : (parent._enabled ? qgcPal.text : qgcPal.colorGrey)
                sourceSize.width: width
            }

            MouseArea {
                anchors.fill: parent
                cursorShape:  parent._enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (!parent._enabled) return
                    if (videoOffsetPopover.visible) {
                        videoOffsetPopover.close()
                    } else {
                        videoOffsetPopover.open()
                    }
                }
            }
        }

        QGCButton {
            text: qsTr("Load Telemetry Log")
            onClicked: pickLogFile()
            visible: !controller.link
        }

        QGCButton {
            text: qsTr("Close")
            onClicked: {
                if (_replay) _replay.closeFlight()
                var activeVehicle = QGroundControl.multiVehicleManager.activeVehicle
                if (activeVehicle) {
                    activeVehicle.closeVehicle()
                }
                QGroundControl.settingsManager.flyViewSettings.showLogReplayStatusBar.rawValue = false
            }
        }
    }

    // ── Video offset popover ──────────────────────────────────────────────────
    Popup {
        id:          videoOffsetPopover
        modal:       false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width:       ScreenTools.defaultFontPixelWidth * 56

        // Position above the camera icon — anchored to the parent bar
        x: _root.width - width - _margins * 2
        y: -height - _margins

        background: Rectangle {
            color:        qgcPal.window
            radius:       8
            border.color: Qt.rgba(1, 1, 1, 0.2)
            border.width: 1
        }

        contentItem: VideoOffsetEditor {
            replay:     _replay
            controller: controller
        }
    }
}
