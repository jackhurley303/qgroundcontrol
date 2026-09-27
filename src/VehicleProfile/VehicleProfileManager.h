#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(VehicleProfileManagerLog)

class QGCFileWatcher;
class QJSEngine;
class QmlObjectListModel;
class QQmlEngine;
class VehicleProfile;
class VehicleProfileEntry;

/// Owns the list of saved vehicles: every `.vehicle` file in one folder, one
/// VehicleProfileEntry per file. Creates, saves (atomically, with QSaveFile) and deletes the
/// files, and reloads the list when a file in the folder changes outside QGC.
///
/// The app instance follows AppSettings::vehicleSavePath(), including a save-path change at
/// runtime. The list is kept sorted by vehicle name.
class VehicleProfileManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_MOC_INCLUDE("QmlObjectListModel.h")
    Q_MOC_INCLUDE("VehicleProfileEntry.h")

    Q_PROPERTY(QmlObjectListModel* vehicles READ vehicles CONSTANT)
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)

public:
    /// Loads every `.vehicle` file in `folder`. There is deliberately no default constructor:
    /// QML prefers a default constructor over create() and would build a second instance.
    explicit VehicleProfileManager(const QString& folder, QObject* parent = nullptr);
    ~VehicleProfileManager() override;

    /// The app instance, which follows AppSettings::vehicleSavePath().
    static VehicleProfileManager* instance();
    static VehicleProfileManager* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    static constexpr const char* kFileExtension = "vehicle";

    /// Creates a vehicle named `name` and writes its file. The file name is `name` made safe
    /// for the file system, with a " (2)", " (3)" ... suffix if that name is taken. Returns
    /// nullptr and logs a warning if the file cannot be written.
    Q_INVOKABLE VehicleProfileEntry* createVehicle(const QString& name);

    /// Writes `entry` to its own file, atomically. The file name never changes, even when the
    /// vehicle was renamed. Returns false and logs a warning on failure.
    Q_INVOKABLE bool saveVehicle(VehicleProfileEntry* entry);

    /// Deletes `entry`'s file and removes it from the list. Returns false and logs a warning
    /// on failure.
    Q_INVOKABLE bool deleteVehicle(VehicleProfileEntry* entry);

    /// Discards every unsaved edit on `entry` by reading its own file back from disk, the way
    /// a rescan reloads a file changed outside QGC. Used to undo a partly-applied edit (for
    /// example a Save that wrote its staged fields to the entry, then failed to write the file)
    /// when the user cancels instead of retrying. Returns false and logs a warning, changing
    /// nothing, if `entry` is not in this list or its file cannot be read. Read-only: never
    /// touches the file, so it never changes what the next rescan sees.
    Q_INVOKABLE bool revertVehicle(VehicleProfileEntry* entry);

    /// Returns the entry with this id, or nullptr.
    Q_INVOKABLE VehicleProfileEntry* vehicleById(const QString& id) const;

    /// Every `mavType` the file format allows, as `{value, text}` maps for a combo box:
    /// `value` is the QGCMAVLinkTypes::VehicleClass_t to write back with
    /// VehicleProfileEntry::setMavType(), `text` is QGCMAVLink::mavTypeToString() for that
    /// value.
    Q_INVOKABLE static QVariantList mavTypes();

    /// The three firmware values `flightController.firmware` can hold, as `{value, text}`
    /// maps for a combo box: `value` is the QGCMAVLinkTypes::FirmwareClass_t to write back
    /// with VehicleProfileEntry::setFlightControllerFirmware(), `text` is
    /// QGCMAVLink::firmwareClassToString() for that value.
    Q_INVOKABLE static QVariantList firmwareTypes();

    QmlObjectListModel* vehicles() const { return _vehicles; }

    QString folder() const { return _folder; }

    /// Switches to another folder: drops every entry and loads the new folder's files.
    void setFolder(const QString& folder);

signals:
    void folderChanged();

private:
    /// What the manager last saw of one file, so a rescan skips a file that has not changed
    /// and the manager's own writes do not count as outside changes.
    struct FileState
    {
        qint64 size = -1;
        QDateTime lastModified;
        QByteArray hash;    ///< of the bytes last loaded or saved; empty if the file never loaded
        QString skippedId;  ///< set when the file was skipped because another file has this id
    };

    void _followAppSettings();
    void _rescan();
    bool _readFile(const QString& filePath, VehicleProfile& profile, FileState& state, QString& errorString) const;
    FileState _statFile(const QString& filePath) const;
    bool _writeFile(const QString& filePath, const VehicleProfile& profile, FileState& state);
    QString _uniqueFilePath(const QString& vehicleName) const;
    void _addEntry(VehicleProfileEntry* entry);
    void _removeEntry(VehicleProfileEntry* entry);
    void _sortEntries();
    void _refreshWatches();

    QString _folder;
    QmlObjectListModel* _vehicles = nullptr;
    QGCFileWatcher* _watcher = nullptr;
    QHash<QString, FileState> _knownFiles;  ///< loaded files and files that failed to load, by path
    QSet<QString> _changedFiles;            ///< files the watcher reported changed since the last rescan
    bool _followingAppSettings = false;
};
