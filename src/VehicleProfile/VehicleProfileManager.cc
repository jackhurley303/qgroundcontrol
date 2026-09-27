#include "VehicleProfileManager.h"

#include <utility>

#include <QtCore/QApplicationStatic>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtQml/QJSEngine>

#include "AppSettings.h"
#include "QGCFileWatcher.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "VehicleProfile.h"
#include "VehicleProfileEntry.h"

QGC_LOGGING_CATEGORY(VehicleProfileManagerLog, "VehicleProfile.VehicleProfileManager")

Q_APPLICATION_STATIC(VehicleProfileManager, _vehicleProfileManagerInstance, QString());

namespace {

/// Leaves room for a " (NN)" suffix and the extension inside the 255-byte file name limit
/// most file systems have, even when every character takes four bytes in UTF-8.
constexpr qsizetype kMaxBaseNameLength = 50;

/// Turns a vehicle name into a file base name every common file system accepts.
QString safeFileBaseName(const QString& vehicleName)
{
    static const QString invalidChars = QStringLiteral("<>:\"/\\|?*");
    // One normal form, so _uniqueFilePath() compares names the way APFS looks them up.
    const QString normalizedName = vehicleName.normalized(QString::NormalizationForm_C);
    QString baseName;
    baseName.reserve(normalizedName.size());
    for (const QChar c : normalizedName) {
        const bool invalid = (c.category() == QChar::Other_Control) || invalidChars.contains(c);
        baseName.append(invalid ? QLatin1Char('_') : c);
    }

    if (baseName.size() > kMaxBaseNameLength) {
        baseName.truncate(kMaxBaseNameLength);
        if (baseName.back().isHighSurrogate()) {
            baseName.chop(1);
        }
    }

    // A leading dot hides the file, and Windows drops a trailing dot or space.
    static const QRegularExpression edgeDotsAndSpaces(QStringLiteral("^[\\s.]+|[\\s.]+$"));
    baseName.remove(edgeDotsAndSpaces);
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("Vehicle");
    }

    // Windows reserves these names with any extension too, so "Aux.Quad" is one of them.
    static const QRegularExpression windowsDeviceName(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\.|$)"),
                                                      QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch deviceMatch = windowsDeviceName.match(baseName);
    if (deviceMatch.hasMatch()) {
        baseName.insert(deviceMatch.capturedEnd(1), QLatin1Char('_'));
    }

    return baseName;
}

}  // namespace

VehicleProfileManager::VehicleProfileManager(const QString& folder, QObject* parent)
    : QObject(parent)
    , _vehicles(new QmlObjectListModel(this))
    , _watcher(new QGCFileWatcher(this))
{
    (void) connect(_watcher, &QGCFileWatcher::fileChanged, this, [this](const QString& path) {
        _changedFiles.insert(path);
        _rescan();
    });
    (void) connect(_watcher, &QGCFileWatcher::directoryChanged, this, [this](const QString&) { _rescan(); });

    setFolder(folder);
}

VehicleProfileManager::~VehicleProfileManager() = default;

VehicleProfileManager* VehicleProfileManager::instance()
{
    VehicleProfileManager* const manager = _vehicleProfileManagerInstance();
    if (!manager->_followingAppSettings) {
        manager->_followAppSettings();
    }
    return manager;
}

VehicleProfileManager* VehicleProfileManager::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);
    VehicleProfileManager* const manager = instance();
    QJSEngine::setObjectOwnership(manager, QJSEngine::CppOwnership);
    return manager;
}

void VehicleProfileManager::_followAppSettings()
{
    _followingAppSettings = true;

    AppSettings* const appSettings = SettingsManager::instance()->appSettings();
    if (!appSettings) {
        qCWarning(VehicleProfileManagerLog) << "No AppSettings, vehicle folder not set";
        return;
    }

    setFolder(appSettings->vehicleSavePath());

    // Queued: AppSettings creates the new Vehicles folder in a slot connected after
    // savePathsChanged, so the folder may not exist yet when the signal arrives.
    (void) connect(
        appSettings, &AppSettings::savePathsChanged, this,
        [this, appSettings]() { setFolder(appSettings->vehicleSavePath()); }, Qt::QueuedConnection);
}

void VehicleProfileManager::setFolder(const QString& folder)
{
    const QString cleanFolder = folder.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    if (cleanFolder == _folder) {
        _rescan();
        return;
    }

    qCDebug(VehicleProfileManagerLog) << "folder:" << cleanFolder;

    _watcher->clear();
    _changedFiles.clear();
    _knownFiles.clear();
    _vehicles->clearAndDeleteContents();

    _folder = cleanFolder;
    emit folderChanged();

    _rescan();
}

VehicleProfileEntry* VehicleProfileManager::createVehicle(const QString& name)
{
    if (_folder.isEmpty() || !QFileInfo(_folder).isDir()) {
        qCWarning(VehicleProfileManagerLog) << "Vehicle folder does not exist:" << _folder;
        return nullptr;
    }

    VehicleProfile profile;
    profile.setName(name);

    const QString filePath = _uniqueFilePath(name);
    FileState state;
    if (!_writeFile(filePath, profile, state)) {
        return nullptr;
    }
    _knownFiles.insert(filePath, state);

    VehicleProfileEntry* const entry = new VehicleProfileEntry(filePath, profile, this);
    _addEntry(entry);
    _sortEntries();
    _refreshWatches();
    return entry;
}

bool VehicleProfileManager::saveVehicle(VehicleProfileEntry* entry)
{
    if (!entry || !_vehicles->contains(entry)) {
        qCWarning(VehicleProfileManagerLog) << "Not a vehicle in this list:" << entry;
        return false;
    }

    FileState state;
    if (!_writeFile(entry->filePath(), entry->profile(), state)) {
        return false;
    }
    _knownFiles.insert(entry->filePath(), state);

    _sortEntries();
    return true;
}

bool VehicleProfileManager::deleteVehicle(VehicleProfileEntry* entry)
{
    if (!entry || !_vehicles->contains(entry)) {
        qCWarning(VehicleProfileManagerLog) << "Not a vehicle in this list:" << entry;
        return false;
    }

    const QString filePath = entry->filePath();
    QFile file(filePath);
    if (file.exists() && !file.remove()) {
        qCWarning(VehicleProfileManagerLog) << "Unable to delete" << filePath << file.errorString();
        return false;
    }

    _knownFiles.remove(filePath);
    _changedFiles.remove(filePath);
    _watcher->unwatchFile(filePath);
    _removeEntry(entry);
    return true;
}

VehicleProfileEntry* VehicleProfileManager::vehicleById(const QString& id) const
{
    for (int i = 0; i < _vehicles->count(); ++i) {
        VehicleProfileEntry* const entry = _vehicles->value<VehicleProfileEntry*>(i);
        if (entry && (entry->id() == id)) {
            return entry;
        }
    }
    return nullptr;
}

void VehicleProfileManager::_rescan()
{
    const QSet<QString> changedFiles = std::exchange(_changedFiles, {});

    QStringList filePaths;
    if (!_folder.isEmpty()) {
        const QDir dir(_folder);
        const QStringList fileNames =
            dir.entryList({QStringLiteral("*.%1").arg(kFileExtension)}, QDir::Files, QDir::Name);
        for (const QString& fileName : fileNames) {
            filePaths.append(dir.filePath(fileName));
        }
    }
    const QSet<QString> presentFiles(filePaths.cbegin(), filePaths.cend());

    // Drop the files that are gone. Iterate a copy, since _removeEntry() changes the list.
    const QObjectList entries = *_vehicles->objectList();
    QList<VehicleProfileEntry*> remainingEntries;
    for (QObject* object : entries) {
        VehicleProfileEntry* const entry = qobject_cast<VehicleProfileEntry*>(object);
        if (!entry) {
            continue;
        }
        if (presentFiles.contains(entry->filePath())) {
            remainingEntries.append(entry);
        } else {
            qCDebug(VehicleProfileManagerLog) << "File removed:" << entry->filePath();
            _removeEntry(entry);
        }
    }
    for (auto it = _knownFiles.begin(); it != _knownFiles.end();) {
        if (presentFiles.contains(it.key())) {
            ++it;
        } else {
            it = _knownFiles.erase(it);
        }
    }

    // Reload the loaded files that changed. They go before new files, so a vehicle already in
    // the list keeps its id when a second file claims the same one.
    for (VehicleProfileEntry* entry : remainingEntries) {
        const QString filePath = entry->filePath();
        const FileState known = _knownFiles.value(filePath);
        const FileState current = _statFile(filePath);
        if (!changedFiles.contains(filePath) && (current.size == known.size) &&
            (current.lastModified == known.lastModified)) {
            continue;
        }

        VehicleProfile profile;
        FileState state;
        QString errorString;
        if (!_readFile(filePath, profile, state, errorString)) {
            // Most likely a write in progress. The watcher reports the file again when the
            // write finishes.
            qCWarning(VehicleProfileManagerLog) << "Keeping the last good copy of" << filePath << "-" << errorString;
            state.hash = known.hash;
            _knownFiles.insert(filePath, state);
            continue;
        }

        if (state.hash == known.hash) {
            _knownFiles.insert(filePath, state);
            continue;
        }

        if (profile.id() != entry->id()) {
            // A different vehicle now lives in this file. Load it as a new entry below, so no
            // entry's id ever changes.
            qCDebug(VehicleProfileManagerLog) << "Vehicle id changed in" << filePath;
            _knownFiles.remove(filePath);
            _removeEntry(entry);
            continue;
        }

        qCDebug(VehicleProfileManagerLog) << "File changed:" << filePath;
        _knownFiles.insert(filePath, state);
        (void) entry->replaceProfile(profile);
    }

    QSet<QString> ids;
    QSet<QString> loadedPaths;
    for (int i = 0; i < _vehicles->count(); ++i) {
        const VehicleProfileEntry* const entry = _vehicles->value<VehicleProfileEntry*>(i);
        ids.insert(entry->id());
        loadedPaths.insert(entry->filePath());
    }

    // Load the new files, and retry the skipped ones that changed.
    for (const QString& filePath : std::as_const(filePaths)) {
        if (loadedPaths.contains(filePath)) {
            continue;
        }

        const auto knownIt = _knownFiles.constFind(filePath);
        if ((knownIt != _knownFiles.cend()) && !changedFiles.contains(filePath)) {
            const FileState current = _statFile(filePath);
            const bool unchanged = (current.size == knownIt->size) && (current.lastModified == knownIt->lastModified);
            const bool stillSkipped = knownIt->skippedId.isEmpty() || ids.contains(knownIt->skippedId);
            if (unchanged && stillSkipped) {
                continue;
            }
        }

        VehicleProfile profile;
        FileState state;
        QString errorString;
        if (!_readFile(filePath, profile, state, errorString)) {
            qCWarning(VehicleProfileManagerLog) << "Skipping" << filePath << "-" << errorString;
            _knownFiles.insert(filePath, state);
            continue;
        }

        if (ids.contains(profile.id())) {
            qCWarning(VehicleProfileManagerLog)
                << "Skipping" << filePath << "- another file already has vehicle id" << profile.id();
            state.skippedId = profile.id();
            _knownFiles.insert(filePath, state);
            continue;
        }

        qCDebug(VehicleProfileManagerLog) << "Loaded:" << filePath;
        _knownFiles.insert(filePath, state);
        ids.insert(profile.id());
        _addEntry(new VehicleProfileEntry(filePath, profile, this));
    }

    _sortEntries();
    _refreshWatches();
}

VehicleProfileManager::FileState VehicleProfileManager::_statFile(const QString& filePath) const
{
    const QFileInfo fileInfo(filePath);
    FileState state;
    state.size = fileInfo.size();
    state.lastModified = fileInfo.lastModified();
    return state;
}

bool VehicleProfileManager::_readFile(const QString& filePath, VehicleProfile& profile, FileState& state,
                                      QString& errorString) const
{
    // Stat before reading: if the file changes after the stat, the next rescan sees a
    // different stat and reads it again.
    state = _statFile(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorString = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (!profile.loadJson(bytes, errorString)) {
        return false;
    }
    state.hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    return true;
}

bool VehicleProfileManager::_writeFile(const QString& filePath, const VehicleProfile& profile, FileState& state)
{
    const QByteArray bytes = profile.toJson();

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || (file.write(bytes) != bytes.size()) || !file.commit()) {
        qCWarning(VehicleProfileManagerLog) << "Unable to save" << filePath << file.errorString();
        return false;
    }

    state = _statFile(filePath);
    state.hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    return true;
}

QString VehicleProfileManager::_uniqueFilePath(const QString& vehicleName) const
{
    const QString baseName = safeFileBaseName(vehicleName);
    const QDir dir(_folder);

    // APFS finds a file whatever its case or Unicode normal form, and NTFS whatever its case.
    // Compare the same way, or QSaveFile would replace another vehicle's file.
    const auto nameKey = [](const QString& name) {
        return name.normalized(QString::NormalizationForm_C).toCaseFolded();
    };
    QSet<QString> takenNames;
    const QStringList existingNames =
        dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString& existingName : existingNames) {
        takenNames.insert(nameKey(existingName));
    }

    QString fileName = QStringLiteral("%1.%2").arg(baseName, kFileExtension);
    for (int suffix = 2; takenNames.contains(nameKey(fileName)); ++suffix) {
        fileName = QStringLiteral("%1 (%2).%3").arg(baseName).arg(suffix).arg(kFileExtension);
    }
    return dir.filePath(fileName);
}

void VehicleProfileManager::_addEntry(VehicleProfileEntry* entry)
{
    _vehicles->append(entry);
}

void VehicleProfileManager::_removeEntry(VehicleProfileEntry* entry)
{
    (void) _vehicles->removeOne(entry);
    entry->deleteLater();
}

void VehicleProfileManager::_sortEntries()
{
    const auto lessThan = [](const VehicleProfileEntry* a, const VehicleProfileEntry* b) {
        const int nameOrder = a->name().compare(b->name(), Qt::CaseInsensitive);
        return (nameOrder != 0) ? (nameOrder < 0) : (a->fileName() < b->fileName());
    };

    // Selection sort that only moves rows up: QmlObjectListModel::move() reports a move down
    // by more than one row to views at the wrong place.
    const int count = _vehicles->count();
    for (int i = 0; i < count; ++i) {
        int first = i;
        for (int j = i + 1; j < count; ++j) {
            if (lessThan(_vehicles->value<VehicleProfileEntry*>(j), _vehicles->value<VehicleProfileEntry*>(first))) {
                first = j;
            }
        }
        if (first != i) {
            _vehicles->move(first, i);
        }
    }
}

void VehicleProfileManager::_refreshWatches()
{
    if (_folder.isEmpty()) {
        return;
    }

    if (QFileInfo(_folder).isDir() && !_watcher->isWatchingDirectory(_folder)) {
        (void) _watcher->watchDirectory(_folder, nullptr);
    }

    const QStringList watchedFiles = _watcher->watchedFiles();
    for (const QString& watchedFile : watchedFiles) {
        if (!_knownFiles.contains(watchedFile)) {
            (void) _watcher->unwatchFile(watchedFile);
        }
    }

    // Re-added on every rescan, since on some platforms replacing a file (as QSaveFile does)
    // ends its watch. Failed files are watched too, so a write that completes one is seen.
    for (auto it = _knownFiles.cbegin(); it != _knownFiles.cend(); ++it) {
        if (QFileInfo::exists(it.key())) {
            (void) _watcher->watchFile(it.key(), nullptr);
        }
    }
}
