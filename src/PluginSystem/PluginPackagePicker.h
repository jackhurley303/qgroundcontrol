/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(PluginPackagePickerLog)

class QGCPluginManager;

/// Installs a plugin package that the user picks with Android's native file picker.
///
/// The picker copies the picked file into stagingDir(). installPickedFile() installs that
/// copy through QGCPluginManager::installPlugin() and then deletes stagingDir(), whatever
/// the outcome. One pick runs at a time: pick() does nothing while one is running.
class PluginPackagePicker : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool picking READ picking NOTIFY pickingChanged)

public:
    explicit PluginPackagePicker(QObject* parent = nullptr);

    /// Opens the native picker. Supported on Android only.
    Q_INVOKABLE void pick();

    /// Installs the package at pickedPath, then deletes stagingDir(). An empty path, which
    /// the picker reports for a cancelled pick or a failed copy, installs nothing.
    /// @return the install error, or an empty string
    QString installPickedFile(const QString& pickedPath);

    bool picking() const { return _picking; }

    /// The folder the picker copies the picked file into, under the cache directory.
    static QString stagingDir();

signals:
    void pickingChanged();
    void installFailed(const QString& errorString);

private:
    void _pickFinished(const QString& pickedPath);
    void _setPicking(bool picking);

    QGCPluginManager* _pluginManager = nullptr;
    bool _picking = false;

    friend class PluginPackagePickerTest;
};
