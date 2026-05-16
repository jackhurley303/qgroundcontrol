import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// Plan-view panel content for the Example plugin.
//
// This component is loaded inside the floating panel managed by PlanViewPluginPanel.
// The panel itself provides the header (title, minimize, pop-out buttons), drag,
// and resize — so this file only needs to contain your plugin's actual UI.
ColumnLayout {
    spacing: ScreenTools.defaultFontPixelHeight * 0.5

    // -------------------------------------------------------------------------
    // Title
    // -------------------------------------------------------------------------
    QGCLabel {
        Layout.fillWidth:    true
        text:                qsTr("Example Plugin")
        font.bold:           true
        horizontalAlignment: Text.AlignHCenter
    }

    // Divider
    Rectangle {
        Layout.fillWidth: true
        height:           1
        color:            Qt.rgba(1, 1, 1, 0.15)
    }

    // -------------------------------------------------------------------------
    // Content — replace everything below with your plugin's UI
    // -------------------------------------------------------------------------
    QGCLabel {
        Layout.fillWidth: true
        text:             qsTr("Replace this content with your plugin's plan-view UI.")
        font.pointSize:   ScreenTools.smallFontPointSize
        wrapMode:         Text.WordWrap
        opacity:          0.7
        horizontalAlignment: Text.AlignHCenter
    }

    // Spacer
    Item { Layout.fillHeight: true }
}
