import QtQuick
import QtQuick.Layouts
import QtCore

import QGroundControl
import QGroundControl.Controls

// Floating plugin panel for the fly view.
// One instance per plugin. Visible only when expanded=true.
// Draggable header, resizable body, minimize button to collapse back to strip.
// Position and size are persisted across sessions.
Item {
    id:             _root
    anchors.fill:   parent

    property var  panelItem:    null
    property int  panelIndex:   0
    property bool expanded:     false
    signal minimized()
    signal poppedOut()

    property real _margin:        ScreenTools.defaultFontPixelHeight / 2
    property real _headerHeight:  ScreenTools.defaultFontPixelHeight * 2
    property real _handleSize:    ScreenTools.defaultFontPixelHeight * 1.5
    property real _minWidth:      ScreenTools.defaultFontPixelWidth * 20
    property real _minHeight:     ScreenTools.defaultFontPixelHeight * 8

    // Default panel size — plugin-provided if non-zero, otherwise framework defaults
    property real _defaultPanelWidth:  (panelItem && panelItem.defaultWidth  > 0)
                                            ? panelItem.defaultWidth  * ScreenTools.defaultFontPixelWidth
                                            : ScreenTools.defaultFontPixelWidth  * 30
    property real _defaultPanelHeight: (panelItem && panelItem.defaultHeight > 0)
                                            ? panelItem.defaultHeight * ScreenTools.defaultFontPixelHeight
                                            : ScreenTools.defaultFontPixelHeight * 15

    // Exposed so FlyViewWidgetLayer can reparent the Loader when popping out to a window.
    property alias contentLoader: _contentLoader
    property alias contentArea:   _contentColumnLayout

    visible: (expanded || _panel.opacity > 0) && !QGroundControl.videoManager.fullScreen
    z:       QGroundControl.zOrderWidgets

    QGCPalette { id: qgcPal }

    // Re-position to right edge (staggered by index) when parent finishes sizing.
    // Clamp the panel to stay fully within the parent whenever the parent resizes.
    // Always derive the target from the saved position so the panel snaps back to
    // where the user left it when the window is enlarged again.
    function _clampPanelToParent() {
        if (width <= 0 || height <= 0) return
        var targetX, targetY
        if (_settings.savedX >= 0) {
            targetX = _settings.savedX
        } else if (panelItem && panelItem.defaultXFraction >= 0) {
            targetX = panelItem.defaultXFraction * (width - _panel.width)
        } else {
            targetX = Math.max(0, width - _panel.width - _margin * 2 - panelIndex * (_panel.width + _margin * 2))
        }
        if (_settings.savedY >= 0) {
            targetY = _settings.savedY
        } else if (panelItem && panelItem.defaultYFraction >= 0) {
            targetY = panelItem.defaultYFraction * (height - _panel.height)
        } else {
            targetY = _margin
        }
        _panel.x = Math.max(0, Math.min(targetX, width  - _panel.width))
        _panel.y = Math.max(0, Math.min(targetY, height - _panel.height))
    }

    onWidthChanged:  _clampPanelToParent()
    onHeightChanged: _clampPanelToParent()

    Settings {
        id:       _settings
        category: "FlyViewPluginPanel_" + (panelItem ? panelItem.pluginId.replace(/[^A-Za-z0-9]/g, "_") : panelIndex)
        property real savedX:      -1
        property real savedY:      -1
        property real savedWidth:  -1
        property real savedHeight: -1
    }

    // -------------------------------------------------------------------------
    // Floating panel
    // -------------------------------------------------------------------------
    Rectangle {
        id:           _panel
        width:        _settings.savedWidth  > 0 ? _settings.savedWidth  : _defaultPanelWidth
        height:       _settings.savedHeight > 0 ? _settings.savedHeight : _defaultPanelHeight
        color:        Qt.rgba(0.08, 0.08, 0.08, 0.88)
        border.color: Qt.rgba(1, 1, 1, 0.12)
        border.width: 1
        radius:       ScreenTools.defaultFontPixelHeight / 2
        clip:         true
        z:            QGroundControl.zOrderWidgets

        opacity:         _root.expanded ? 1.0 : 0.0
        scale:           _root.expanded ? 1.0 : 0.88
        transformOrigin: Item.TopRight

        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        Behavior on scale   { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }

        Component.onCompleted: _root._clampPanelToParent()

        DeadMouseArea { anchors.fill: parent }

        // --- Header ---
        Rectangle {
            id:             _header
            anchors.top:    parent.top
            anchors.left:   parent.left
            anchors.right:  parent.right
            height:         _headerHeight
            color:          Qt.rgba(0, 0, 0, 0.25)

            // Bottom separator
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left:   parent.left
                anchors.right:  parent.right
                height:         1
                color:          Qt.rgba(1, 1, 1, 0.1)
            }

            // Plugin name label
            QGCLabel {
                anchors.left:           parent.left
                anchors.right:          _popOutBtn.left
                anchors.leftMargin:     _margin
                anchors.rightMargin:    _margin
                anchors.verticalCenter: parent.verticalCenter
                font.bold:              true
                color:                  "white"
                elide:                  Text.ElideRight
                text:                   panelItem ? panelItem.name : ""
            }

            // Pop-out button — opens panel content in a native OS window
            Rectangle {
                id:             _popOutBtn
                anchors.right:  _minimizeBtn.left
                anchors.top:    parent.top
                anchors.bottom: parent.bottom
                width:          _headerHeight
                color:          _popOutMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                visible:        !ScreenTools.isMobile
                z:              10

                QGCColoredImage {
                    anchors.centerIn: parent
                    width:            parent.height * 0.5
                    height:           width
                    sourceSize.width: width
                    source:           "/qmlimages/FloatingWindow.svg"
                    fillMode:         Image.PreserveAspectFit
                    color:            "white"
                }

                MouseArea {
                    id:           _popOutMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    _root.poppedOut()
                }
            }

            // Minimize button — full header height, high z so it beats the corner resize handle
            Rectangle {
                id:                     _minimizeBtn
                anchors.right:          parent.right
                anchors.top:            parent.top
                anchors.bottom:         parent.bottom
                width:                  _headerHeight
                color:                  _minimizeMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                z:                      10

                QGCLabel {
                    anchors.centerIn: parent
                    text:             "−"
                    color:            "white"
                    font.pixelSize:   ScreenTools.defaultFontPixelHeight * 0.9
                }

                MouseArea {
                    id:           _minimizeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    _root.minimized()
                }
            }

            // Drag area
            MouseArea {
                anchors.fill:        parent
                anchors.rightMargin: _minimizeBtn.width + _margin * 2
                cursorShape:         Qt.SizeAllCursor
                drag.target:         _panel
                drag.minimumX:       0
                drag.maximumX:       _root.width  - _panel.width
                drag.minimumY:       0
                drag.maximumY:       _root.height - _panel.height
                onReleased: {
                    _settings.savedX = _panel.x
                    _settings.savedY = _panel.y
                }
            }
        }

        // --- Content ---
        ColumnLayout {
            id:              _contentColumnLayout
            anchors.top:     _header.bottom
            anchors.left:    parent.left
            anchors.right:   parent.right
            anchors.bottom:  parent.bottom
            anchors.margins: _margin
            spacing:         _margin

            Loader {
                id:                _contentLoader
                Layout.fillWidth:  true
                Layout.fillHeight: true
                source:            panelItem ? panelItem.panelUrl : ""
                onLoaded: {
                    if (!item) return
                    if (item.hasOwnProperty("planMasterControllerFlyView"))
                        item.planMasterControllerFlyView  = globals.planMasterControllerFlyView
                    if (item.hasOwnProperty("planMasterControllerPlanView"))
                        item.planMasterControllerPlanView = globals.planMasterControllerPlanView
                }
            }
        }

        // ---- Resize handles ----
        // Desktop: thin edge + corner handles on all 8 sides.
        // Mobile:  single bottom-right corner handle with canvas grip visual.
        // All handles clamp to parent bounds so the panel never leaves the screen.

        property real _resizeEdge: Math.max(6, ScreenTools.defaultFontPixelHeight * 0.5)

        // Clamp helpers — keep panel fully inside _root
        function _resizeTop(startPY, startH, dy) {
            var bottom  = startPY + startH
            var newY    = Math.max(0, Math.min(bottom - _minHeight, startPY + dy))
            _panel.y      = newY
            _panel.height = bottom - newY
        }
        function _resizeBottom(startPY, startH, dy) {
            _panel.height = Math.max(_minHeight, Math.min(_root.height - startPY, startH + dy))
        }
        function _resizeLeft(startPX, startW, dx) {
            var right  = startPX + startW
            var newX   = Math.max(0, Math.min(right - _minWidth, startPX + dx))
            _panel.x     = newX
            _panel.width = right - newX
        }
        function _resizeRight(startPX, startW, dx) {
            _panel.width = Math.max(_minWidth, Math.min(_root.width - startPX, startW + dx))
        }

        // --- Top edge ---
        MouseArea {
            anchors.top:    parent.top
            anchors.left:   parent.left
            anchors.right:  parent.right
            height:         _panel._resizeEdge
            cursorShape:    Qt.SizeVerCursor
            visible:        !ScreenTools.isMobile
            z:              2
            property real _sy; property real _startH; property real _startPY
            onPressed:         { var p = mapToItem(null, mouseX, mouseY); _sy = p.y; _startH = _panel.height; _startPY = _panel.y }
            onPositionChanged: { if (!pressed) return; _panel._resizeTop(_startPY, _startH, mapToItem(null, mouseX, mouseY).y - _sy) }
            onReleased:        { _settings.savedHeight = _panel.height; _settings.savedY = _panel.y }
        }

        // --- Bottom edge ---
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.left:   parent.left
            anchors.right:  parent.right
            height:         _panel._resizeEdge
            cursorShape:    Qt.SizeVerCursor
            visible:        !ScreenTools.isMobile
            z:              2
            property real _sy; property real _startH; property real _startPY
            onPressed:         { var p = mapToItem(null, mouseX, mouseY); _sy = p.y; _startH = _panel.height; _startPY = _panel.y }
            onPositionChanged: { if (!pressed) return; _panel._resizeBottom(_startPY, _startH, mapToItem(null, mouseX, mouseY).y - _sy) }
            onReleased:        { _settings.savedHeight = _panel.height }
        }

        // --- Left edge ---
        MouseArea {
            anchors.left:   parent.left
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            width:          _panel._resizeEdge
            cursorShape:    Qt.SizeHorCursor
            visible:        !ScreenTools.isMobile
            z:              2
            property real _sx; property real _startW; property real _startPX
            onPressed:         { var p = mapToItem(null, mouseX, mouseY); _sx = p.x; _startW = _panel.width; _startPX = _panel.x }
            onPositionChanged: { if (!pressed) return; _panel._resizeLeft(_startPX, _startW, mapToItem(null, mouseX, mouseY).x - _sx) }
            onReleased:        { _settings.savedWidth = _panel.width; _settings.savedX = _panel.x }
        }

        // --- Right edge ---
        MouseArea {
            anchors.right:  parent.right
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            width:          _panel._resizeEdge
            cursorShape:    Qt.SizeHorCursor
            visible:        !ScreenTools.isMobile
            z:              2
            property real _sx; property real _startW; property real _startPX
            onPressed:         { var p = mapToItem(null, mouseX, mouseY); _sx = p.x; _startW = _panel.width; _startPX = _panel.x }
            onPositionChanged: { if (!pressed) return; _panel._resizeRight(_startPX, _startW, mapToItem(null, mouseX, mouseY).x - _sx) }
            onReleased:        { _settings.savedWidth = _panel.width }
        }

        // --- Top-left corner ---
        MouseArea {
            anchors.top:  parent.top
            anchors.left: parent.left
            width:  _panel._resizeEdge;  height: _panel._resizeEdge
            cursorShape: Qt.SizeFDiagCursor
            visible:     !ScreenTools.isMobile
            z: 3
            property real _sx; property real _sy; property real _startW; property real _startH; property real _startPX; property real _startPY
            onPressed: { var p = mapToItem(null, mouseX, mouseY); _sx = p.x; _sy = p.y; _startW = _panel.width; _startH = _panel.height; _startPX = _panel.x; _startPY = _panel.y }
            onPositionChanged: {
                if (!pressed) return
                var p = mapToItem(null, mouseX, mouseY)
                _panel._resizeLeft(_startPX, _startW, p.x - _sx)
                _panel._resizeTop(_startPY, _startH, p.y - _sy)
            }
            onReleased: { _settings.savedWidth = _panel.width; _settings.savedHeight = _panel.height; _settings.savedX = _panel.x; _settings.savedY = _panel.y }
        }

        // --- Top-right corner ---
        MouseArea {
            anchors.top:   parent.top
            anchors.right: parent.right
            width:  _panel._resizeEdge;  height: _panel._resizeEdge
            cursorShape: Qt.SizeBDiagCursor
            visible:     !ScreenTools.isMobile
            z: 3
            property real _sx; property real _sy; property real _startW; property real _startH; property real _startPX; property real _startPY
            onPressed: { var p = mapToItem(null, mouseX, mouseY); _sx = p.x; _sy = p.y; _startW = _panel.width; _startH = _panel.height; _startPX = _panel.x; _startPY = _panel.y }
            onPositionChanged: {
                if (!pressed) return
                var p = mapToItem(null, mouseX, mouseY)
                _panel._resizeRight(_startPX, _startW, p.x - _sx)
                _panel._resizeTop(_startPY, _startH, p.y - _sy)
            }
            onReleased: { _settings.savedWidth = _panel.width; _settings.savedHeight = _panel.height; _settings.savedY = _panel.y }
        }

        // --- Bottom-left corner ---
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.left:   parent.left
            width:  _panel._resizeEdge;  height: _panel._resizeEdge
            cursorShape: Qt.SizeBDiagCursor
            visible:     !ScreenTools.isMobile
            z: 3
            property real _sx; property real _sy; property real _startW; property real _startH; property real _startPX; property real _startPY
            onPressed: { var p = mapToItem(null, mouseX, mouseY); _sx = p.x; _sy = p.y; _startW = _panel.width; _startH = _panel.height; _startPX = _panel.x; _startPY = _panel.y }
            onPositionChanged: {
                if (!pressed) return
                var p = mapToItem(null, mouseX, mouseY)
                _panel._resizeLeft(_startPX, _startW, p.x - _sx)
                _panel._resizeBottom(_startPY, _startH, p.y - _sy)
            }
            onReleased: { _settings.savedWidth = _panel.width; _settings.savedHeight = _panel.height; _settings.savedX = _panel.x }
        }

        // --- Bottom-right corner ---
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.right:  parent.right
            width:  _panel._resizeEdge;  height: _panel._resizeEdge
            cursorShape: Qt.SizeFDiagCursor
            visible:     !ScreenTools.isMobile
            z: 3
            property real _sx; property real _sy; property real _startW; property real _startH; property real _startPX; property real _startPY
            onPressed: { var p = mapToItem(null, mouseX, mouseY); _sx = p.x; _sy = p.y; _startW = _panel.width; _startH = _panel.height; _startPX = _panel.x; _startPY = _panel.y }
            onPositionChanged: {
                if (!pressed) return
                var p = mapToItem(null, mouseX, mouseY)
                _panel._resizeRight(_startPX, _startW, p.x - _sx)
                _panel._resizeBottom(_startPY, _startH, p.y - _sy)
            }
            onReleased: { _settings.savedWidth = _panel.width; _settings.savedHeight = _panel.height }
        }

        // --- Mobile: bottom-right corner handle + grip visual ---
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.right:  parent.right
            width:          _handleSize
            height:         _handleSize
            visible:        ScreenTools.isMobile
            z:              3
            property real _sx; property real _sy; property real _startW; property real _startH; property real _startPX; property real _startPY
            onPressed: {
                var p = mapToItem(null, mouseX, mouseY)
                _sx = p.x; _sy = p.y; _startW = _panel.width; _startH = _panel.height; _startPX = _panel.x; _startPY = _panel.y
            }
            onPositionChanged: {
                if (!pressed) return
                var p = mapToItem(null, mouseX, mouseY)
                _panel._resizeRight(_startPX, _startW, p.x - _sx)
                _panel._resizeBottom(_startPY, _startH, p.y - _sy)
            }
            onReleased: { _settings.savedWidth = _panel.width; _settings.savedHeight = _panel.height }
        }

        Canvas {
            anchors.right:  parent.right
            anchors.bottom: parent.bottom
            width:          _handleSize
            height:         _handleSize
            visible:        ScreenTools.isMobile
            z:              4
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = "white"
                ctx.globalAlpha = 0.5
                ctx.lineWidth   = 1.5
                for (var i = 1; i <= 3; i++) {
                    ctx.beginPath()
                    ctx.moveTo(width,         height - i * 5)
                    ctx.lineTo(width - i * 5, height)
                    ctx.stroke()
                }
            }
        }

    }
}
