/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// The Browse section of the Plugins page: the catalog URL, one card per catalog plugin, and a
/// detail view with the screenshots and release notes. Installing goes through `installer`,
/// the PluginCatalogInstaller that owns the consent dialog.
SettingsGroupLayout {
    id: root

    Layout.fillWidth:       true
    // Bounds the whole page: wrapped labels report their one-line width as implicit width,
    // and with the Installed group hidden these labels would otherwise set the page width.
    Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 60
    heading:                qsTr("Browse plugins")

    required property var installer

    property var _pluginSettings:   QGroundControl.settingsManager.pluginSettings
    // The detail view shows the entry with this id, looked up again on every change so it
    // follows an install that finishes while it is open.
    property string _detailId:      ""
    readonly property var _detail:  installer.entryFor(_detailId)
    readonly property bool _idle:   PluginCatalogManager.installingId === ""

    // The URL last read. Leaving the field also ends editing, so it reads the catalog again
    // only when the URL changed or the last read failed.
    property string _fetchedUrl:    ""

    // A fetch that no longer lists the plugin closes its detail view for good, so a later
    // fetch that lists it again does not reopen the view by itself.
    on_DetailChanged: {
        if (_detail === null && _detailId !== "") {
            _detailId = ""
        }
    }

    // An index may name a local file by path, which Image cannot load without a scheme.
    function _imageSource(location) {
        if (location.startsWith("/")) {
            return "file://" + location
        }
        if (/^[A-Za-z]:[\\/]/.test(location)) {
            return "file:///" + location.replace(/\\/g, "/")
        }
        return location
    }

    function _sizeText(bytes) {
        if (bytes < 1024) {
            return qsTr("%1 B").arg(bytes)
        }
        if (bytes < 1024 * 1024) {
            return qsTr("%1 KB").arg((bytes / 1024).toFixed(1))
        }
        return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
    }

    function _versionText(entry) {
        if (!entry.installed) {
            return entry.version
        }
        return entry.updateAvailable ? qsTr("Installed %1, %2 available").arg(entry.installedVersion).arg(entry.version) :
                                       qsTr("Installed %1").arg(entry.installedVersion)
    }

    Component.onCompleted: _fetchedUrl = _pluginSettings.catalogUrl.valueString

    RowLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelWidth

        QGCLabel { text: qsTr("Catalog URL") }

        FactTextField {
            objectName:         "pluginCatalogUrlField"
            Layout.fillWidth:   true
            fact:               root._pluginSettings.catalogUrl
            onUpdated: {
                // A failed read is retried on the same URL, so Enter in the field is a retry.
                if (fact.valueString !== root._fetchedUrl || PluginCatalogManager.fetchState === PluginCatalogManager.Failed) {
                    root._fetchedUrl = fact.valueString
                    PluginCatalogManager.fetch()
                }
            }
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        wrapMode:           Text.Wrap
        font.italic:        true
        visible:            root._pluginSettings.catalogUrl.valueString === ""
        text:               qsTr("The plugin catalog is turned off. Enter a catalog URL to browse plugins.")
    }

    QGCLabel {
        Layout.fillWidth:   true
        wrapMode:           Text.Wrap
        font.italic:        true
        visible:            PluginCatalogManager.fetchState === PluginCatalogManager.Fetching
        text:               qsTr("Reading the catalog...")
    }

    QGCLabel {
        objectName:         "pluginCatalogEmpty"
        Layout.fillWidth:   true
        wrapMode:           Text.Wrap
        font.italic:        true
        visible:            PluginCatalogManager.fetchState === PluginCatalogManager.Ready && PluginCatalogManager.entries.length === 0
        text:               qsTr("The catalog lists no plugins.")
    }

    // The card list

    Repeater {
        model: root._detail === null ? PluginCatalogManager.entries : []

        RowLayout {
            required property var modelData

            objectName:         "pluginCatalogCard_" + modelData.id
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            Image {
                Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 3
                Layout.preferredHeight: Layout.preferredWidth
                Layout.alignment:       Qt.AlignTop
                visible:                modelData.icon !== ""
                source:                 modelData.icon !== "" ? root._imageSource(modelData.icon) : ""
                fillMode:               Image.PreserveAspectFit
                asynchronous:           true
            }

            ColumnLayout {
                Layout.fillWidth:   true
                spacing:            0

                QGCLabel {
                    Layout.fillWidth:   true
                    text:               modelData.name
                    font.bold:          true
                    wrapMode:           Text.Wrap
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    text:               qsTr("%1 · %2").arg(modelData.author).arg(modelData.compatible ? modelData.tier + " · " + root._versionText(modelData) : qsTr("not compatible with this version"))
                    font.pointSize:     ScreenTools.smallFontPointSize
                    wrapMode:           Text.Wrap
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    text:               modelData.summary
                    wrapMode:           Text.Wrap
                    visible:            modelData.summary !== ""
                }
            }

            QGCButton {
                objectName:     "pluginCatalogDetailsButton_" + modelData.id
                text:           qsTr("Details")
                onClicked:      root._detailId = modelData.id
            }

            QGCButton {
                objectName:     "pluginCatalogInstallButton_" + modelData.id
                text:           modelData.installed ? qsTr("Update") : qsTr("Install")
                visible:        modelData.compatible && (!modelData.installed || modelData.updateAvailable)
                enabled:        root._idle
                onClicked:      root.installer.requestInstall(modelData)
            }
        }
    }

    // The detail view

    ColumnLayout {
        objectName:         "pluginCatalogDetail"
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelHeight / 2
        visible:            root._detail !== null

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                objectName: "pluginCatalogDetailBackButton"
                text:       qsTr("Back")
                onClicked:  root._detailId = ""
            }

            QGCLabel {
                Layout.fillWidth:   true
                text:               root._detail ? root._detail.name : ""
                font.pointSize:     ScreenTools.mediumFontPointSize
                font.bold:          true
                wrapMode:           Text.Wrap
            }

            QGCButton {
                objectName: "pluginCatalogDetailInstallButton"
                text:       root._detail && root._detail.installed ? qsTr("Update") : qsTr("Install")
                visible:    root._detail !== null && root._detail.compatible && (!root._detail.installed || root._detail.updateAvailable)
                enabled:    root._idle
                onClicked:  root.installer.requestInstall(root._detail)
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.Wrap
            text:               root._detail ? qsTr("%1 · %2").arg(root._detail.author).arg(root._detail.compatible ? root._detail.tier + " · " + root._versionText(root._detail) : qsTr("not compatible with this version")) : ""
        }

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.Wrap
            text:               root._detail ? root._detail.description : ""
            visible:            text !== ""
        }

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.Wrap
            font.pointSize:     ScreenTools.smallFontPointSize
            visible:            root._detail !== null && root._detail.compatible
            text:               root._detail && root._detail.compatible ? qsTr("Version %1, released %2, %3").arg(root._detail.version).arg(root._detail.released).arg(root._sizeText(root._detail.size)) : ""
        }

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.Wrap
            font.pointSize:     ScreenTools.smallFontPointSize
            visible:            text !== ""
            text:               root._detail && root._detail.license !== "" ? qsTr("License: %1").arg(root._detail.license) : ""
        }

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.Wrap
            font.pointSize:     ScreenTools.smallFontPointSize
            visible:            text !== ""
            text:               root._detail && root._detail.homepage !== "" ? qsTr("Homepage: %1").arg(root._detail.homepage) : ""
        }

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.Wrap
            font.pointSize:     ScreenTools.smallFontPointSize
            visible:            text !== ""
            text:               root._detail && root._detail.repository !== "" ? qsTr("Repository: %1").arg(root._detail.repository) : ""
        }

        ListView {
            objectName:             "pluginCatalogScreenshots"
            Layout.fillWidth:       true
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 12
            orientation:            ListView.Horizontal
            spacing:                ScreenTools.defaultFontPixelWidth
            clip:                   true
            visible:                count > 0
            model:                  root._detail ? root._detail.screenshots : []

            delegate: Image {
                height:         ListView.view.height
                source:         root._imageSource(modelData)
                fillMode:       Image.PreserveAspectFit
                asynchronous:   true
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("What is new")
            font.bold:          true
            visible:            notesLabel.visible
        }

        QGCLabel {
            id:                 notesLabel
            objectName:         "pluginCatalogDetailNotes"
            Layout.fillWidth:   true
            wrapMode:           Text.Wrap
            text:               root._detail && root._detail.compatible ? root._detail.notes : ""
            visible:            text !== ""
        }
    }
}
