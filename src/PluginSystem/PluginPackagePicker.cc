/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PluginPackagePicker.h"

#include <QtCore/QDir>
#include <QtCore/QStandardPaths>

#include "QGCLoggingCategory.h"
#include "QGCPluginManager.h"

#ifdef Q_OS_ANDROID
#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>

#include "AndroidInterface.h"
#endif

QGC_LOGGING_CATEGORY(PluginPackagePickerLog, "PluginSystem.PluginPackagePicker")

PluginPackagePicker::PluginPackagePicker(QObject* parent)
    : QObject(parent)
    , _pluginManager(QGCPluginManager::instance())
{}

QString PluginPackagePicker::stagingDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("PluginPackageStaging"));
}

void PluginPackagePicker::pick()
{
    if (_picking) {
        qCDebug(PluginPackagePickerLog) << "A pick is already running";
        return;
    }

#ifdef Q_OS_ANDROID
    // Starts empty, so a copy orphaned by an earlier run cannot outlive this pick.
    const QString dir = stagingDir();
    if (!QDir(dir).removeRecursively() || !QDir().mkpath(dir)) {
        qCWarning(PluginPackagePickerLog) << "Cannot prepare the staging folder" << dir;
        emit installFailed(tr("Cannot prepare a folder for the picked package"));
        return;
    }

    _setPicking(true);

    // The callback usually runs on a Java thread after the user closes the picker. It is queued
    // onto the application object, not this one, so the copy is deleted even when this
    // object is gone by then.
    QPointer<PluginPackagePicker> self = this;
    AndroidInterface::openFileImportDialog(
        dir,
        [self](const QString& pickedPath) {
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, pickedPath]() {
                    if (self) {
                        self->_pickFinished(pickedPath);
                    } else {
                        (void) QDir(stagingDir()).removeRecursively();
                    }
                },
                Qt::QueuedConnection);
        },
        QStringLiteral(".qgcplugin"));
#else
    qCWarning(PluginPackagePickerLog) << "The native picker is only supported on Android";
#endif
}

QString PluginPackagePicker::installPickedFile(const QString& pickedPath)
{
    QString error;
    if (pickedPath.isEmpty()) {
        qCDebug(PluginPackagePickerLog) << "Nothing to install: the pick was cancelled or the copy failed";
    } else if (!_pluginManager) {
        error = tr("The plugin manager is not available");
    } else {
        error = _pluginManager->installPlugin(pickedPath);
    }

    // A failed copy can leave a partial file, so the whole folder goes, not only pickedPath.
    const QString dir = stagingDir();
    if (!QDir(dir).removeRecursively()) {
        qCWarning(PluginPackagePickerLog) << "Cannot delete the staging folder" << dir;
    }

    return error;
}

void PluginPackagePicker::_pickFinished(const QString& pickedPath)
{
    _setPicking(false);

    const QString error = installPickedFile(pickedPath);
    if (!error.isEmpty()) {
        emit installFailed(error);
    }
}

void PluginPackagePicker::_setPicking(bool picking)
{
    if (_picking != picking) {
        _picking = picking;
        emit pickingChanged();
    }
}
