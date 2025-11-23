import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl.Controls

/// Example plugin view demonstrating custom tool UI
Rectangle {
    color: "#2c2c2c"

    ColumnLayout {
        anchors.fill:           parent
        anchors.margins:        20
        spacing:                10

        QGCLabel {
            text:               "Example Plugin"
            font.pointSize:     24
            font.bold:          true
            color:              "#ffffff"
            Layout.alignment:   Qt.AlignHCenter
        }

        QGCLabel {
            text:               "This is an example plugin demonstrating the QGC plugin architecture."
            wrapMode:           Text.WordWrap
            color:              "#ffffff"
            Layout.fillWidth:   true
            Layout.alignment:   Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            Layout.fillWidth:       true
            Layout.preferredHeight: 1
            color:                  "#555555"
        }

        QGCLabel {
            text:               "Plugin Information"
            font.bold:          true
            color:              "#ffffff"
        }

        QGCLabel {
            text:               "• Plugin loaded successfully"
            color:              "#ffffff"
            Layout.leftMargin:  40
        }

        QGCLabel {
            text:               "• Plugin interface version: 1"
            color:              "#ffffff"
            Layout.leftMargin:  40
        }

        QGCLabel {
            text:               "• Custom tool menu item added"
            color:              "#ffffff"
            Layout.leftMargin:  40
        }

        QGCLabel {
            text:               "• Resources initialized and loaded"
            color:              "#ffffff"
            Layout.leftMargin:  40
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
