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
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// What the Installed and Browse sections of the Plugins page share: the catalog fetch error,
/// the progress and result of the running install, and the consent dialog every install and
/// update goes through. Starts the fetch when the page opens.
ColumnLayout {
    id: root

    Layout.fillWidth:   true
    spacing:            ScreenTools.defaultFontPixelHeight / 2

    // The last install's outcome, kept so the banner still says it after the entries refresh.
    property string _resultText:    ""
    property bool   _resultIsError: false

    /// Ask the user to consent to installing or updating the plugin in `entry`, a
    /// PluginCatalogManager entry, and start the install on accept.
    function requestInstall(entry) {
        consentDialogFactory.open({ entry: entry })
    }

    /// The current PluginCatalogManager entry for pluginId, or null. Read through a function so a
    /// binding that calls it re-evaluates when the entries change.
    function entryFor(pluginId) {
        const entries = PluginCatalogManager.entries
        for (let i = 0; i < entries.length; i++) {
            if (entries[i].id === pluginId) {
                return entries[i]
            }
        }
        return null
    }

    // install() offers the newest version in the catalog as it is when called, which a fetch
    // that finished while the dialog was open may have changed. Install only what the user saw;
    // otherwise ask again about what is on offer now.
    function _startInstall(shown) {
        const current = entryFor(shown.id)
        if (current === null || !current.compatible) {
            _resultText     = qsTr("%1 is no longer available from the catalog.").arg(shown.name)
            _resultIsError  = true
            return
        }
        if (current.version !== shown.version || current.tier !== shown.tier || current.installed !== shown.installed) {
            requestInstall(current)
            return
        }
        _resultText = ""
        const error = PluginCatalogManager.install(current.id)
        if (error.length > 0) {
            _resultText     = qsTr("Could not start installing %1: %2").arg(current.name).arg(error)
            _resultIsError  = true
        }
    }

    function _nameFor(pluginId) {
        const entry = entryFor(pluginId)
        return entry ? entry.name : pluginId
    }

    QGCPalette { id: qgcPal }

    Component.onCompleted: PluginCatalogManager.fetch()

    Connections {
        target: PluginCatalogManager

        // A new read of the catalog, from the page opening or a URL edit, starts a clean banner.
        function onFetchStateChanged() {
            if (PluginCatalogManager.fetchState === PluginCatalogManager.Fetching) {
                root._resultText = ""
            }
        }

        function onInstallFinished(pluginId, success, errorString, staged) {
            const name = root._nameFor(pluginId)
            root._resultIsError = !success
            if (!success) {
                root._resultText = qsTr("Installing %1 failed: %2").arg(name).arg(errorString)
            } else if (staged) {
                root._resultText = qsTr("The update to %1 is ready. Restart QGroundControl to finish the update.").arg(name)
            } else {
                root._resultText = qsTr("%1 is installed.").arg(name)
            }
        }
    }

    QGCLabel {
        objectName:         "pluginCatalogFetchError"
        Layout.fillWidth:       true
        // Without a preferred width the unwrapped text sets the page's minimum width.
        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 50
        // Failed keeps the entries of the last good read, so this line sits above the old
        // cards rather than replacing them.
        visible:            PluginCatalogManager.fetchState === PluginCatalogManager.Failed
        text:               PluginCatalogManager.fetchError
        wrapMode:           Text.Wrap
        color:              qgcPal.colorRed
    }

    ColumnLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelHeight / 4
        visible:            PluginCatalogManager.installingId !== ""

        QGCLabel {
            objectName:         "pluginCatalogInstallingLabel"
            Layout.fillWidth:       true
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 50
            text:               qsTr("Installing %1...").arg(root._nameFor(PluginCatalogManager.installingId))
            wrapMode:           Text.Wrap
        }

        ProgressBar {
            objectName:         "pluginCatalogInstallProgress"
            Layout.fillWidth:   true
            from:               0
            to:                 1
            value:              PluginCatalogManager.installProgress
        }
    }

    QGCLabel {
        objectName:             "pluginCatalogInstallResult"
        Layout.fillWidth:       true
        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 50
        visible:            text !== "" && PluginCatalogManager.installingId === ""
        text:               root._resultText
        wrapMode:           Text.Wrap
        color:              root._resultIsError ? qgcPal.colorRed : qgcPal.colorGreen
    }

    QGCPopupDialogFactory {
        id:                 consentDialogFactory
        dialogComponent:    consentDialogComponent
    }

    Component {
        id: consentDialogComponent

        // Accepting is the consent to run the plugin: it is what the file dialog is for a
        // manual install, so the text names who made it and what kind of code it is.
        QGCPopupDialog {
            id:         consentDialog
            title:      consentDialog.entry.installed ? qsTr("Update %1?").arg(consentDialog.entry.name) :
                                                        qsTr("Install %1?").arg(consentDialog.entry.name)
            buttons:    Dialog.Ok | Dialog.Cancel

            property var entry: ({})

            onAccepted: root._startInstall(consentDialog.entry)

            ColumnLayout {
                // A plain width: the dialog's content parent is an Item, which ignores Layout.*
                width:      ScreenTools.defaultFontPixelWidth * 55
                spacing:    ScreenTools.defaultFontPixelHeight / 2

                QGCLabel {
                    objectName:         "pluginCatalogConsentText"
                    Layout.fillWidth:   true
                    wrapMode:           Text.Wrap
                    text:               consentDialog.entry.tier === "sdk" ?
                                            qsTr("This plugin runs native code on your computer, with the same access as QGroundControl itself. Install it only if you trust its author.") :
                                            qsTr("This plugin adds QML pages to QGroundControl. Install it only if you trust its author.")
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.Wrap
                    text:               qsTr("Author: %1").arg(consentDialog.entry.author)
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.Wrap
                    text:               qsTr("Repository: %1").arg(consentDialog.entry.repository)
                    visible:            consentDialog.entry.repository !== ""
                }

                QGCLabel {
                    Layout.fillWidth:   true
                    wrapMode:           Text.Wrap
                    text:               qsTr("Type: %1 plugin, version %2").arg(consentDialog.entry.tier).arg(consentDialog.entry.version)
                }
            }
        }
    }
}
