import QtQuick

import QGroundControl.Controls

// Custom dock item for the Example plugin.
//
// This component fills the row allocated by the fly-view dock strip.
// Use it to show your plugin name, status indicators, notification badges, etc.
// The size of this item is controlled by the dock strip — do not set width/height here.
Item {
    anchors.fill: parent

    // Plugin name label — centered in the row
    Text {
        anchors.centerIn: parent
        text:             "Example"
        color:            "white"
        opacity:          0.9
        font.pointSize:   ScreenTools.smallFontPointSize
    }

}
