import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Settings -> Vehicles: list every saved VehicleProfileEntry, and add, edit and delete them.
/// VehicleProfileManager (src/VehicleProfile) owns the file I/O. The editor dialog below holds
/// every field as its own staged copy and only writes VehicleProfileEntry's Q_PROPERTYs at
/// Save - see the dialog's own comment for why.
SettingsPage {
    id: root

    QGCPalette { id: qgcPal }

    // {value, text} maps from VehicleProfileManager - value is the int VehicleProfileEntry's
    // mavType / flightControllerFirmware property takes, text is the translated display
    // string. Every picker below binds the int value and shows the text; the translated text
    // is never written back or stored.
    property var _mavTypeOptions:  VehicleProfileManager.mavTypes()
    property var _firmwareOptions: VehicleProfileManager.firmwareTypes()

    property bool _showInactive: false

    function _textForValue(options, value) {
        for (var i = 0; i < options.length; i++) {
            if (options[i].value === value) {
                return options[i].text
            }
        }
        return ""
    }

    function _indexForValue(options, value) {
        for (var i = 0; i < options.length; i++) {
            if (options[i].value === value) {
                return i
            }
        }
        return -1
    }

    // How many saved vehicles the current "show inactive" setting leaves visible. Used to
    // pick between the two empty-list messages below - zero vehicles ever saved reads
    // differently from every saved vehicle being hidden by the filter.
    function _visibleVehicleCount() {
        var count = 0
        for (var i = 0; i < VehicleProfileManager.vehicles.count; i++) {
            var entry = VehicleProfileManager.vehicles.get(i)
            if (entry.active || root._showInactive) {
                count++
            }
        }
        return count
    }

    // entry is null to add a new vehicle; VehicleProfileManager.createVehicle() is not called
    // until the dialog's own Save, so opening the editor here never touches disk.
    function _openEditor(entry) {
        vehicleEditDialogFactory.open({ editingEntry: entry, _isNew: entry === null })
    }

    SettingsGroupLayout {
        Layout.fillWidth:       true
        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 60
        heading:                qsTr("Vehicles")
        headingDescription:     qsTr("Vehicle files are stored in the '%1' folder.").arg(QGroundControl.settingsManager.appSettings.vehicleSavePath)

        QGCCheckBoxSlider {
            objectName:         "showInactiveVehiclesToggle"
            Layout.fillWidth:   true
            text:               qsTr("Show inactive vehicles")
            checked:            root._showInactive
            onCheckedChanged:   root._showInactive = checked
        }

        Repeater {
            model: VehicleProfileManager.vehicles

            RowLayout {
                Layout.fillWidth:   true
                visible:            object.active || root._showInactive
                opacity:            object.active ? 1.0 : 0.5
                objectName:         "vehicleRow_" + object.id
                spacing:            ScreenTools.defaultFontPixelWidth

                Image {
                    Layout.preferredWidth:  ScreenTools.minTouchPixels
                    Layout.preferredHeight: ScreenTools.minTouchPixels
                    fillMode:               Image.PreserveAspectFit
                    visible:                object.hasImage
                    source:                 object.hasImage ? object.imageDataUrl : ""
                    objectName:             "vehicleThumbnail_" + object.id
                }

                ColumnLayout {
                    Layout.fillWidth:   true
                    spacing:            0

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               object.name
                        objectName:         "vehicleNameLabel_" + object.id
                    }

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               root._textForValue(root._mavTypeOptions, object.mavType)
                        font.pointSize:     ScreenTools.smallFontPointSize
                        color:              qgcPal.text
                    }
                }

                QGCColoredImage {
                    height:                 ScreenTools.minTouchPixels
                    width:                  height
                    sourceSize.height:      height
                    fillMode:               Image.PreserveAspectFit
                    mipmap:                 true
                    smooth:                 true
                    color:                  qgcPalEdit.text
                    source:                 "/res/pencil.svg"

                    QGCPalette { id: qgcPalEdit; colorGroupEnabled: parent.enabled }

                    QGCMouseArea {
                        objectName: "editVehicleButton_" + object.id
                        fillItem:   parent
                        onClicked:  root._openEditor(object)
                    }
                }

                QGCColoredImage {
                    height:                 ScreenTools.minTouchPixels
                    width:                  height
                    sourceSize.height:      height
                    fillMode:               Image.PreserveAspectFit
                    mipmap:                 true
                    smooth:                 true
                    color:                  qgcPalDelete.text
                    source:                 "/res/TrashDelete.svg"

                    QGCPalette { id: qgcPalDelete; colorGroupEnabled: parent.enabled }

                    QGCMouseArea {
                        objectName: "deleteVehicleButton_" + object.id
                        fillItem:   parent
                        onClicked:  QGroundControl.showMessageDialog(
                                        root,
                                        qsTr("Delete Vehicle"),
                                        qsTr("Are you sure you want to delete '%1'? This deletes its file from disk.").arg(object.name),
                                        Dialog.Ok | Dialog.Cancel,
                                        function () {
                                            if (!VehicleProfileManager.deleteVehicle(object)) {
                                                QGroundControl.showMessageDialog(root, qsTr("Delete Vehicle"), qsTr("Unable to delete the vehicle file. The vehicle stays in the list."), Dialog.Ok)
                                            }
                                        })
                    }
                }
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            VehicleProfileManager.vehicles.count === 0
            text:               qsTr("No vehicles saved yet.")
            font.italic:        true
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            VehicleProfileManager.vehicles.count > 0 && root._visibleVehicleCount() === 0
            text:               qsTr("No active vehicles. Turn on \"Show inactive vehicles\" to see them.")
            font.italic:        true
        }

        LabelledButton {
            label:      qsTr("Add New Vehicle")
            buttonText: qsTr("Add")

            Component.onCompleted: objectName = "addVehicleRow"

            onClicked: root._openEditor(null)
        }
    }

    QGCPopupDialogFactory {
        id: vehicleEditDialogFactory
        dialogComponent: vehicleEditDialogComponent
    }

    Component {
        id: vehicleEditDialogComponent

        // Every field here is staged on the dialog itself (the _xxx properties below) and
        // only copied onto editingEntry, then written to disk, when the user clicks Save
        // (onAccepted). Nothing here ever binds a field's displayed value to editingEntry
        // directly: VehicleProfileEntry shares one profileChanged signal across every
        // property, so a live binding on one field re-fires on every OTHER field's write and
        // reverts whatever the user is mid-typing.
        // A plain Cancel (nothing was ever staged onto editingEntry) still needs no undo. A
        // Cancel AFTER a failed Save is different: by the time Save can fail, the image
        // and/or the staged fields may already have landed on editingEntry in memory, even
        // though the write to disk did not happen - onAccepted/onRejected below cover that
        // case with _needsRevertOnCancel and VehicleProfileManager.revertVehicle().
        QGCPopupDialog {
            id:                     editDialog
            objectName:             "vehicleEditDialog"
            title:                  qsTr("Edit Vehicle")
            buttons:                Dialog.Save | Dialog.Cancel
            acceptButtonEnabled:    nameField.text.length > 0

            // Set by vehicleEditDialogFactory.open(). null means "add a new vehicle" - no
            // VehicleProfileManager.createVehicle() call has happened yet, and Save makes
            // one. Once editingEntry is a real entry (either passed in to edit an existing
            // vehicle, or created by this dialog's own Save), it can still go null if the
            // file it backs is removed from under it - an outside change, or the save path
            // changing - because VehicleProfileManager drops the entry of a file that is gone;
            // the countChanged Connections below closes the dialog outright when that happens.
            property var editingEntry: null

            // True only until this dialog's own Save creates the entry. Sets isNew back to
            // false immediately once VehicleProfileManager.createVehicle() succeeds, so a
            // second Save click after a save failure does not create a second file.
            property bool _isNew: false

            // True only for the duration of _applyStagedToEntry()'s own writes, so this
            // dialog's Connections below can tell its own Save from a genuine outside change
            // and not show the outside-change message or reload over its own in-progress
            // write.
            property bool _applyingSave: false

            // Set when a Save on an EXISTING vehicle (editingEntry was passed in, not created
            // by this dialog) writes some staged fields and/or the staged image onto the
            // entry and then fails on a later step - the entry is left with unsaved edits in
            // memory even though onAccepted returns without closing. Cancel checks this and
            // calls VehicleProfileManager.revertVehicle() to put the file's own values back,
            // so a later Save cannot persist edits the user already cancelled once. A failed
            // Save on a NEW vehicle does not use this flag - see onAccepted.
            property bool _needsRevertOnCancel: false

            // Set when a failed Save on a new vehicle could not delete the file its own
            // createVehicle() wrote. Cancel then retries that delete instead of a revert.
            property bool _deleteOnCancel: false

            // QGCFlickableScrollIndicator draws the vertical scrollbar as an overlay inside the
            // flickable's own width, not in a separate reserved column (see
            // QGCFlickableScrollIndicator.qml). formColumn below fills that width exactly, so
            // without this gutter the indicator sits on top of the form's rightmost fields
            // whenever the dialog scrolls. formColumnContainer reserves this much extra width
            // past formColumn's own right edge for the indicator to sit in instead.
            property real _scrollbarGutterWidth: ScreenTools.defaultFontPixelWidth

            // Assigns every staged battery/sensor row a small integer identity that survives
            // add/remove/reorder, since the array index a row happens to sit at can change
            // between the moment a text field's onEditingFinished closes over it and the
            // moment that handler actually runs (removing an earlier row reshuffles every
            // later one). _updateBattery/_removeBattery and their sensor equivalents look a
            // row up by this key, never by index, so a stale closure edits the row it meant to
            // and not whatever row now sits at that index.
            property int _nextRowKey: 0

            function _withRowKey(row) {
                var keyed = Object.assign({}, row)
                keyed._key = _nextRowKey
                _nextRowKey += 1
                return keyed
            }

            function _withoutRowKey(row) {
                var stripped = Object.assign({}, row)
                delete stripped._key
                return stripped
            }

            property string _name
            property string _manufacturer
            property string _model
            property bool   _active
            property int    _mavType
            property double _weightKg
            property double _maxPayloadKg
            property int    _maxFlightTimeMinutes
            property string _registrationNumber
            property string _serialNumber
            property var    _batteries: []
            property var    _sensors:   []
            property string _fcHardware
            property int    _fcFirmware
            property string _fcFirmwareVersion
            property string _notes

            // The staged image edit. importImage()/clearImage() are methods that decode bytes
            // or drop them, not plain values, so there is nothing to hold except which of the
            // two the user asked for and, for an import, the path to read at Save time.
            // _pendingImagePath non-empty means "import this file on Save"; _pendingClearImage
            // means "clear the image on Save"; both empty/false means "leave the image alone".
            property string _pendingImagePath:  ""
            property bool   _pendingClearImage: false

            function _stageImportImage(path) {
                _pendingImagePath = path
                _pendingClearImage = false
            }

            function _stageClearImage() {
                _pendingImagePath = ""
                _pendingClearImage = true
            }

            // QGCFileDialogController.localFileToUrl() assumes a plain local path and always
            // wraps it with QUrl::fromLocalFile(), which double-wraps a path already typed or
            // pasted as a file: URL (imagePathField takes free-form text, not just a native
            // picker's result) - the result is not a URL either side loads, so the preview
            // goes blank. Pass an already-schemed string through unchanged.
            function _imagePreviewSource(path) {
                if (path.length === 0) {
                    return ""
                }
                return path.indexOf("file:") === 0 ? path : QGCFileDialogController.localFileToUrl(path)
            }

            // True while either a pending import or the entry's own saved image would show in
            // the preview right now - shared by the preview's visible/source and by the Clear
            // button's enabled, so the three stay in agreement.
            function _hasImageToShow() {
                if (_pendingImagePath.length > 0) {
                    return true
                }
                if (_pendingClearImage) {
                    return false
                }
                return editingEntry !== null && editingEntry.hasImage
            }

            // Fills every staged property from entry (or from add-mode defaults when entry is
            // null), then pushes each value into its field imperatively. Nothing here is a
            // QML binding, so calling this again later - the outside-change path - genuinely
            // replaces whatever the user had typed: a file changed outside QGC discards the
            // unsaved edits here instead of silently mixing with them.
            function _loadFromEntry(entry) {
                _name                   = entry ? entry.name : qsTr("New Vehicle")
                _manufacturer           = entry ? entry.manufacturer : ""
                _model                  = entry ? entry.model : ""
                _active                 = entry ? entry.active : true
                _mavType                = entry ? entry.mavType : 0
                _weightKg               = entry ? entry.weightKg : 0
                _maxPayloadKg           = entry ? entry.maxPayloadKg : 0
                _maxFlightTimeMinutes   = entry ? entry.maxFlightTimeMinutes : 0
                _registrationNumber     = entry ? entry.registrationNumber : ""
                _serialNumber           = entry ? entry.serialNumber : ""
                _batteries              = (entry ? entry.batteries : []).map(editDialog._withRowKey)
                _sensors                = (entry ? entry.sensors : []).map(editDialog._withRowKey)
                _fcHardware             = entry ? entry.flightControllerHardware : ""
                _fcFirmware             = entry ? entry.flightControllerFirmware : 0
                _fcFirmwareVersion      = entry ? entry.flightControllerFirmwareVersion : ""
                _notes                  = entry ? entry.notes : ""

                nameField.text                  = _name
                manufacturerField.text          = _manufacturer
                modelField.text                 = _model
                activeToggle.checked            = _active
                mavTypeCombo.currentIndex       = root._indexForValue(root._mavTypeOptions, _mavType)
                weightField.text                = QGroundControl.unitsConversion.gramsToAppSettingsWeightUnits(_weightKg * 1000).toFixed(3)
                maxPayloadField.text            = QGroundControl.unitsConversion.gramsToAppSettingsWeightUnits(_maxPayloadKg * 1000).toFixed(3)
                maxFlightTimeField.text         = _maxFlightTimeMinutes.toString()
                fcHardwareField.text            = _fcHardware
                firmwareCombo.currentIndex      = root._indexForValue(root._firmwareOptions, _fcFirmware)
                fcFirmwareVersionField.text     = _fcFirmwareVersion
                registrationNumberField.text    = _registrationNumber
                serialNumberField.text          = _serialNumber
                notesField.text                 = _notes

                // An outside change discards a pending image edit the same as every staged
                // field - the file on disk just replaced what this pending edit was based on.
                _pendingImagePath   = ""
                _pendingClearImage  = false
                imagePathField.text = ""
            }

            Component.onCompleted: editDialog._loadFromEntry(editDialog.editingEntry)

            function _editingEntryIsListed() {
                for (var i = 0; i < VehicleProfileManager.vehicles.count; i++) {
                    if (VehicleProfileManager.vehicles.get(i) === editingEntry) {
                        return true
                    }
                }
                return false
            }

            // QML cannot connect to QObject::destroyed. The manager removes an entry from its
            // list, which emits countChanged, before it deletes the entry, so watch the list.
            Connections {
                target: VehicleProfileManager.vehicles

                function onCountChanged() {
                    if (editDialog.editingEntry && !editDialog._editingEntryIsListed()) {
                        editDialog.editingEntry = null
                        editDialog.close()
                        QGroundControl.showMessageDialog(
                                    root,
                                    qsTr("Vehicle Removed"),
                                    qsTr("This vehicle is no longer in the list: its file was removed or the vehicle folder changed. Any unsaved edits were discarded."),
                                    Dialog.Ok)
                    }
                }
            }

            Connections {
                target: editDialog.editingEntry

                function onProfileChanged() {
                    if (editDialog._applyingSave) {
                        return
                    }
                    editDialog._loadFromEntry(editDialog.editingEntry)
                    QGroundControl.showMessageDialog(
                                editDialog,
                                qsTr("Vehicle Changed"),
                                qsTr("This vehicle changed on disk. Any unsaved edits here were discarded."),
                                Dialog.Ok)
                }
            }

            // Does not touch _applyingSave itself - onAccepted brackets this call together
            // with the pending image apply that runs before it, so the outside-change
            // Connections above sees one uninterrupted save, not two.
            function _applyStagedToEntry(entry) {
                entry.name = _name
                entry.manufacturer = _manufacturer
                entry.model = _model
                entry.active = _active
                entry.mavType = _mavType
                entry.weightKg = _weightKg
                entry.maxPayloadKg = _maxPayloadKg
                entry.maxFlightTimeMinutes = _maxFlightTimeMinutes
                entry.registrationNumber = _registrationNumber
                entry.serialNumber = _serialNumber
                entry.batteries = _batteries.map(editDialog._withoutRowKey)
                entry.sensors = _sensors.map(editDialog._withoutRowKey)
                entry.flightControllerHardware = _fcHardware
                entry.flightControllerFirmware = _fcFirmware
                entry.flightControllerFirmwareVersion = _fcFirmwareVersion
                entry.notes = _notes
            }

            onAccepted: {
                // Captured before createVehicle() below can flip _isNew to false: which
                // failure-recovery path applies (delete the file this Save created, or keep
                // the entry that already existed for a revert on Cancel) depends on how this
                // attempt started, not on _isNew's value after it.
                var wasAdd = _isNew
                var entry = editingEntry
                if (wasAdd) {
                    entry = VehicleProfileManager.createVehicle(_name.length > 0 ? _name : qsTr("New Vehicle"))
                    if (!entry) {
                        preventClose = true
                        QGroundControl.showMessageDialog(editDialog, qsTr("Add Vehicle"), qsTr("Unable to create a new vehicle file."), Dialog.Ok)
                        return
                    }
                    // The entry now exists on disk - stop treating this as an add so a save
                    // failure below and a retry do not create a second file.
                    editingEntry = entry
                    _isNew = false
                }
                if (!entry) {
                    return
                }

                _needsRevertOnCancel = false
                _applyingSave = true

                // The image import/clear runs first, before any staged field is written to
                // entry: importImage() changes nothing on a failure (a bad path, an unreadable
                // or non-image file - the likelier failure of the two), so a failure here
                // leaves entry exactly as it was, nothing to undo either way below.
                var imageOk = true
                if (_pendingImagePath.length > 0) {
                    imageOk = entry.importImage(_pendingImagePath)
                } else if (_pendingClearImage) {
                    entry.clearImage()
                }

                var saveOk = false
                if (imageOk) {
                    _applyStagedToEntry(entry)
                    saveOk = VehicleProfileManager.saveVehicle(entry)
                }
                _applyingSave = false

                if (imageOk && saveOk) {
                    return
                }

                if (wasAdd) {
                    // createVehicle() above already wrote a file for this vehicle. Add+Cancel
                    // (or a plain retry) must not leave that name-only file behind, so undo the
                    // create at once instead of waiting for Cancel, and put the dialog back in
                    // the same Add state it started this attempt in. The staged field values
                    // and the still-pending image edit are left alone, so the user's typing
                    // survives a retry.
                    // editingEntry is cleared first so the countChanged Connections above
                    // does not treat this dialog's own delete as an outside removal.
                    editingEntry = null
                    if (VehicleProfileManager.deleteVehicle(entry)) {
                        _isNew = true
                    } else {
                        // The file is still there. Keep editing it, so a retry saves to it
                        // rather than creating a second file, and Cancel tries the delete again.
                        editingEntry = entry
                        _deleteOnCancel = true
                        _needsRevertOnCancel = true
                    }
                } else {
                    // entry existed before this dialog opened. The image and/or the staged
                    // fields above already landed on it in memory even though the save did
                    // not reach disk - onRejected below reverts them if the user cancels
                    // instead of fixing the problem and retrying Save.
                    _needsRevertOnCancel = true
                }

                preventClose = true
                if (!imageOk) {
                    QGroundControl.showMessageDialog(editDialog, qsTr("Import Image"), qsTr("Unable to read that image file."), Dialog.Ok)
                } else {
                    QGroundControl.showMessageDialog(editDialog, qsTr("Save Vehicle"), qsTr("Unable to save this vehicle file."), Dialog.Ok)
                }
            }

            onRejected: {
                if (!_needsRevertOnCancel || !editingEntry) {
                    return
                }
                if (_deleteOnCancel) {
                    var entry = editingEntry
                    editingEntry = null
                    if (!VehicleProfileManager.deleteVehicle(entry)) {
                        VehicleProfileManager.revertVehicle(entry)
                        QGroundControl.showMessageDialog(root, qsTr("Add Vehicle"), qsTr("Unable to delete the new vehicle file. The vehicle stays in the list."), Dialog.Ok)
                    }
                    return
                }
                // Bracketed the same way onAccepted's own writes are, so the Connections
                // above treats this as our own change and does not reload over it or show the
                // "changed on disk" message for a revert we asked for ourselves.
                _applyingSave = true
                var reverted = VehicleProfileManager.revertVehicle(editingEntry)
                _applyingSave = false
                if (!reverted) {
                    preventClose = true
                    QGroundControl.showMessageDialog(editDialog, qsTr("Edit Vehicle"), qsTr("Unable to read this vehicle file to discard the unsaved edits. Save to keep the edits, or Cancel to try again."), Dialog.Ok)
                    return
                }
                _needsRevertOnCancel = false
            }

            // key identifies the row (see _withRowKey above), never its current position -
            // removing an earlier row must not make an in-flight edit on a later row land on
            // the wrong one.

            function _addBattery() {
                var batteries = _batteries.slice()
                batteries.push(editDialog._withRowKey({ cellCount: 0, capacityMah: 0 }))
                _batteries = batteries
            }

            function _removeBattery(key) {
                _batteries = _batteries.filter(function (battery) { return battery._key !== key })
            }

            // A field commit edits the row object inside the staged array and never assigns a
            // new array: _batteries and _sensors are the Repeaters' models, and a new model
            // rebuilds every row, which destroys the field the user just moved focus to. The
            // rows show their own typed text, so nothing needs to be told about the change.
            function _updateRow(rows, key, field, value) {
                for (var i = 0; i < rows.length; i++) {
                    if (rows[i]._key === key) {
                        rows[i][field] = value
                        return
                    }
                }
            }

            function _updateBattery(key, field, value) {
                _updateRow(_batteries, key, field, value)
            }

            function _addSensor() {
                var sensors = _sensors.slice()
                sensors.push(editDialog._withRowKey({ type: "", model: "" }))
                _sensors = sensors
            }

            function _removeSensor(key) {
                _sensors = _sensors.filter(function (sensor) { return sensor._key !== key })
            }

            function _updateSensor(key, field, value) {
                _updateRow(_sensors, key, field, value)
            }

            // QGCPopupDialog's own QGCFlickable scrolls this form when it is taller than the
            // window. See _scrollbarGutterWidth above for why formColumn sits inside this
            // wider container rather than being the flickable's content directly.
            Item {
                id:     formColumnContainer
                width:  formColumn.width + editDialog._scrollbarGutterWidth
                height: formColumn.height

                ColumnLayout {
                    id:         formColumn
                    width:      ScreenTools.defaultFontPixelWidth * 60
                    spacing:    ScreenTools.defaultFontPixelHeight / 2

                    SectionHeader {
                        Layout.fillWidth:   true
                        text:               qsTr("Identity")
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Name") }
                        QGCTextField {
                            id:                 nameField
                            objectName:         "vehicleNameField"
                            Layout.fillWidth:   true
                            placeholderText:    qsTr("Enter name")
                            onEditingFinished:  editDialog._name = text
                        }
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Manufacturer") }
                        QGCTextField {
                            id:                 manufacturerField
                            objectName:         "vehicleManufacturerField"
                            Layout.fillWidth:   true
                            onEditingFinished:  editDialog._manufacturer = text
                        }

                        QGCLabel { text: qsTr("Model") }
                        QGCTextField {
                            id:                 modelField
                            objectName:         "vehicleModelField"
                            Layout.fillWidth:   true
                            onEditingFinished:  editDialog._model = text
                        }
                    }

                    QGCCheckBoxSlider {
                        id:                 activeToggle
                        objectName:         "vehicleActiveToggle"
                        Layout.fillWidth:   true
                        text:               qsTr("Active")
                        onCheckedChanged:   editDialog._active = checked
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        Image {
                            objectName:             "vehicleImagePreview"
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 6
                            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 6
                            fillMode:               Image.PreserveAspectFit
                            visible:                editDialog._hasImageToShow()
                            source:                 editDialog._pendingImagePath.length > 0
                                                        ? editDialog._imagePreviewSource(editDialog._pendingImagePath)
                                                        : (editDialog._hasImageToShow() ? editingEntry.imageDataUrl : "")
                        }

                        ColumnLayout {
                            Layout.fillWidth: true

                            // The image is staged like every other field: Import and Clear only
                            // record which one the user asked for (_pendingImagePath /
                            // _pendingClearImage below); onAccepted is the only place that calls
                            // entry.importImage() / clearImage(), after createVehicle in Add mode
                            // and alongside every other staged write. importImage() takes a file
                            // path rather than bytes, so the path itself is the staged value -
                            // there is nothing to decode until Save.
                            //
                            // The "Browse..." button opens the platform's native file picker
                            // (QGCFileDialog -> QtQuick.Dialogs FileDialog on desktop), which UI
                            // automation cannot reach or drive. The path field plus "Import"
                            // button is the reachable route: paste or type a path, then Import -
                            // no native dialog involved.
                            RowLayout {
                                Layout.fillWidth: true

                                QGCTextField {
                                    id:                 imagePathField
                                    objectName:         "vehicleImagePathField"
                                    Layout.fillWidth:   true
                                    placeholderText:    qsTr("Image file path")
                                }

                                QGCButton {
                                    objectName: "vehicleImageBrowseButton"
                                    text:       qsTr("Browse…")
                                    onClicked:  imagePickerDialog.openForLoad()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                QGCButton {
                                    objectName: "vehicleImageImportButton"
                                    text:       qsTr("Import")
                                    enabled:    imagePathField.text.length > 0
                                    onClicked:  editDialog._stageImportImage(imagePathField.text)
                                }

                                QGCButton {
                                    objectName: "vehicleImageClearButton"
                                    text:       qsTr("Clear Image")
                                    enabled:    editDialog._hasImageToShow()
                                    onClicked:  editDialog._stageClearImage()
                                }
                            }
                        }
                    }

                    QGCFileDialog {
                        id:             imagePickerDialog
                        title:          qsTr("Select vehicle image")
                        nameFilters:    [ qsTr("Images (*.png *.jpg *.jpeg *.bmp *.gif)"), qsTr("All Files (*)") ]

                        // file is a local path, the same form Import passes, so picking a file
                        // stages it at once.
                        onAcceptedForLoad: (file) => {
                            imagePathField.text = file
                            editDialog._stageImportImage(file)
                        }
                    }

                    SectionHeader {
                        Layout.fillWidth:   true
                        text:               qsTr("Frame")
                    }

                    LabelledComboBox {
                        id:                     mavTypeCombo
                        Layout.fillWidth:       true
                        label:                  qsTr("Type")
                        // Without a set width the combo sizes to its longest MAV_TYPE text and
                        // pushes every row of the form past the dialog's right edge.
                        comboBoxPreferredWidth: ScreenTools.defaultFontPixelWidth * 30
                        // A property binding, not an imperative Component.onCompleted
                        // assignment, so it is guaranteed set before _loadFromEntry() below
                        // sets currentIndex regardless of completion order between this combo
                        // and editDialog itself.
                        model:                  root._mavTypeOptions.map(function (option) { return option.text })

                        Component.onCompleted: comboBox.objectName = "vehicleMavTypeCombo"

                        onActivated: (index) => {
                            editDialog._mavType = root._mavTypeOptions[index].value
                        }
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Weight") }
                        QGCTextField {
                            id:                     weightField
                            objectName:             "vehicleWeightField"
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 10
                            validator:              DoubleValidator { bottom: 0; decimals: 3 }
                            onEditingFinished:      editDialog._weightKg = QGroundControl.unitsConversion.appSettingsWeightUnitsToGrams(parseFloat(text) || 0) / 1000
                        }
                        QGCLabel { text: QGroundControl.unitsConversion.appSettingsWeightUnitsString }

                        QGCLabel { text: qsTr("Max Payload") }
                        QGCTextField {
                            id:                     maxPayloadField
                            objectName:             "vehicleMaxPayloadField"
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 10
                            validator:              DoubleValidator { bottom: 0; decimals: 3 }
                            onEditingFinished:      editDialog._maxPayloadKg = QGroundControl.unitsConversion.appSettingsWeightUnitsToGrams(parseFloat(text) || 0) / 1000
                        }
                        QGCLabel { text: QGroundControl.unitsConversion.appSettingsWeightUnitsString }
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Max Flight Time") }
                        QGCTextField {
                            id:                     maxFlightTimeField
                            objectName:             "vehicleMaxFlightTimeField"
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 10
                            validator:              IntValidator { bottom: 0 }
                            onEditingFinished:      editDialog._maxFlightTimeMinutes = parseInt(text) || 0
                        }
                        QGCLabel { text: qsTr("minutes") }
                    }

                    SectionHeader {
                        Layout.fillWidth:   true
                        text:               qsTr("Battery")
                    }

                    Repeater {
                        model: editDialog._batteries

                        RowLayout {
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelWidth

                            QGCLabel { text: qsTr("Cells") }
                            QGCTextField {
                                objectName:             "batteryCellCountField_" + index
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 8
                                text:                   modelData.cellCount.toString()
                                validator:              IntValidator { bottom: 0 }
                                onEditingFinished:      editDialog._updateBattery(modelData._key, "cellCount", parseInt(text) || 0)
                            }

                            QGCLabel { text: qsTr("Capacity") }
                            QGCTextField {
                                objectName:             "batteryCapacityField_" + index
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 10
                                text:                   modelData.capacityMah.toString()
                                validator:              IntValidator { bottom: 0 }
                                onEditingFinished:      editDialog._updateBattery(modelData._key, "capacityMah", parseInt(text) || 0)
                            }
                            QGCLabel { text: qsTr("mAh") }

                            QGCColoredImage {
                                height:                 ScreenTools.minTouchPixels
                                width:                  height
                                sourceSize.height:      height
                                fillMode:               Image.PreserveAspectFit
                                mipmap:                 true
                                smooth:                 true
                                color:                  qgcPal.text
                                source:                 "/res/TrashDelete.svg"

                                QGCMouseArea {
                                    objectName: "batteryDeleteButton_" + index
                                    fillItem:   parent
                                    // Taking focus first commits a field still being edited in
                                    // another row before the removal rebuilds every row.
                                    onClicked: {
                                        forceActiveFocus()
                                        editDialog._removeBattery(modelData._key)
                                    }
                                }
                            }
                        }
                    }

                    LabelledButton {
                        label:      qsTr("Batteries")
                        buttonText: qsTr("Add Battery")

                        Component.onCompleted: objectName = "addBatteryRow"

                        onClicked: editDialog._addBattery()
                    }

                    SectionHeader {
                        Layout.fillWidth:   true
                        text:               qsTr("Flight Controller")
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Hardware") }
                        QGCTextField {
                            id:                 fcHardwareField
                            objectName:         "vehicleFcHardwareField"
                            Layout.fillWidth:   true
                            onEditingFinished:  editDialog._fcHardware = text
                        }
                    }

                    LabelledComboBox {
                        id:                     firmwareCombo
                        Layout.fillWidth:       true
                        label:                  qsTr("Firmware")
                        comboBoxPreferredWidth: ScreenTools.defaultFontPixelWidth * 30
                        // See mavTypeCombo above: a property binding, not
                        // Component.onCompleted, so completion order with editDialog cannot
                        // matter.
                        model:                  root._firmwareOptions.map(function (option) { return option.text })

                        Component.onCompleted: comboBox.objectName = "vehicleFirmwareCombo"

                        onActivated: (index) => {
                            editDialog._fcFirmware = root._firmwareOptions[index].value
                        }
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Firmware Version") }
                        QGCTextField {
                            id:                 fcFirmwareVersionField
                            objectName:         "vehicleFcFirmwareVersionField"
                            Layout.fillWidth:   true
                            onEditingFinished:  editDialog._fcFirmwareVersion = text
                        }
                    }

                    SectionHeader {
                        Layout.fillWidth:   true
                        text:               qsTr("Registration")
                    }

                    RowLayout {
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel { text: qsTr("Registration #") }
                        QGCTextField {
                            id:                 registrationNumberField
                            objectName:         "vehicleRegistrationNumberField"
                            Layout.fillWidth:   true
                            onEditingFinished:  editDialog._registrationNumber = text
                        }

                        QGCLabel { text: qsTr("Serial #") }
                        QGCTextField {
                            id:                 serialNumberField
                            objectName:         "vehicleSerialNumberField"
                            Layout.fillWidth:   true
                            onEditingFinished:  editDialog._serialNumber = text
                        }
                    }

                    SectionHeader {
                        Layout.fillWidth:   true
                        text:               qsTr("Sensors")
                    }

                    Repeater {
                        model: editDialog._sensors

                        RowLayout {
                            Layout.fillWidth:   true
                            spacing:            ScreenTools.defaultFontPixelWidth

                            QGCTextField {
                                objectName:             "sensorTypeField_" + index
                                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 12
                                placeholderText:        qsTr("Type")
                                text:                   modelData.type
                                onEditingFinished:      editDialog._updateSensor(modelData._key, "type", text)
                            }
                            QGCTextField {
                                objectName:             "sensorModelField_" + index
                                Layout.fillWidth:       true
                                placeholderText:        qsTr("Model")
                                text:                   modelData.model
                                onEditingFinished:      editDialog._updateSensor(modelData._key, "model", text)
                            }
                            QGCColoredImage {
                                height:                 ScreenTools.minTouchPixels
                                width:                  height
                                sourceSize.height:      height
                                fillMode:               Image.PreserveAspectFit
                                mipmap:                 true
                                smooth:                 true
                                color:                  qgcPal.text
                                source:                 "/res/TrashDelete.svg"

                                QGCMouseArea {
                                    objectName: "sensorDeleteButton_" + index
                                    fillItem:   parent
                                    // See batteryDeleteButton above.
                                    onClicked: {
                                        forceActiveFocus()
                                        editDialog._removeSensor(modelData._key)
                                    }
                                }
                            }
                        }
                    }

                    LabelledButton {
                        label:      qsTr("Sensors")
                        buttonText: qsTr("Add Sensor")

                        Component.onCompleted: objectName = "addSensorRow"

                        onClicked: editDialog._addSensor()
                    }

                    SectionHeader {
                        Layout.fillWidth:   true
                        text:               qsTr("Notes")
                    }

                    TextArea {
                        id:                     notesField
                        objectName:             "vehicleNotesField"
                        Layout.fillWidth:       true
                        Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 4
                        font.pointSize:         ScreenTools.defaultFontPointSize
                        color:                  qgcPal.textFieldText
                        background:             Rectangle { color: qgcPal.textField }
                        onEditingFinished:      editDialog._notes = text
                    }
                }
            }
        }
    }
}
