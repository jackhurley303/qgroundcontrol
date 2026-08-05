import QtQuick

import QGroundControl.PluginUI

// Custom dock item for the fly-view plugin strip.
//
// This component fills the row allocated by the dock strip.
// Use it to show your plugin name, status indicators, notification badges, etc.
// The size of this item is controlled by the dock strip — do not set width/height here.
Item {
    anchors.fill: parent

    Text {
        anchors.centerIn: parent
        text:             "Example"
        color:            "white"
        opacity:          0.9
        font.pointSize:   ScreenTools.smallFontPointSize
    }
}
