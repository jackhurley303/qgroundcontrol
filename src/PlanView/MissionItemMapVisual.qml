import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap

/// Mission item map visual
Item {
    id: _root

    property var map        ///< Map control to place item in
    property var vehicle    ///< Vehicle associated with this item
    property bool interactive: true    ///< Vehicle associated with this item
    property MissionItemIndicatorGroup indicatorGroup

    signal clicked(int sequenceNumber)

    Loader {
        id: mapVisualLoader

        asynchronous: true

        // A delegate removed while its asynchronous load is pending keeps the Loader alive until deleteLater, with
        // an invalid context. When the load then completes, the Loader warns "Cannot create a component in an
        // invalid context". An inactive Loader ignores the completed load.
        Component.onDestruction: active = false

        Component.onCompleted: {
            const properties = {
                map: _root.map,
                vehicle: _root.vehicle,
                opacity: Qt.binding(() => _root.opacity),
                interactive: Qt.binding(() => _root.interactive)
            }
            if (object.isSimpleItem) {
                properties.indicatorGroup = _root.indicatorGroup
            }
            mapVisualLoader.setSource(object.mapVisualQML, properties)
        }

        onLoaded: {
            if (!item) {
                return
            }

            item.parent = map

            if (item.clicked) {
                item.clicked.connect(_root.clicked)
            }
        }
    }
}
