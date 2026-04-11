/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

#include "QGCReplayExtension.h"

Q_DECLARE_LOGGING_CATEGORY(QGCPluginLog)

/**
 * @class QGCPlugin
 * @brief Base class for runtime QGroundControl plugins
 *
 * This is the base class for all runtime-loaded plugins. Plugins extend
 * QGroundControl functionality by providing additional tool menu items,
 * settings, and custom UI components.
 *
 * Unlike QGCCorePlugin (which is a singleton managing the core application),
 * QGCPlugin instances represent individual runtime plugins loaded from
 * shared libraries.
 */
class QGCPlugin : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("")

public:
    explicit QGCPlugin(QObject *parent = nullptr);
    ~QGCPlugin() override;

    /// Initialize the plugin
    /// Called after the plugin is loaded and before it's used
    virtual void init() { }

    /// Cleanup the plugin
    /// Called before the plugin is unloaded
    virtual void cleanup() { }

    /// The tool menu item provided by this plugin
    /// Returns a QVariantMap with keys: title, icon, source, visible
    /// @return A tool menu item
    virtual QVariantMap toolMenuItem() const { return QVariantMap(); }

    /// Returns the plugin's flight replay extension, or nullptr if this plugin
    /// does not provide replay functionality.
    virtual QGCReplayExtension* replayExtension() const { return nullptr; }

    /// Get the plugin's display name
    /// @return Human-readable plugin name
    virtual QString name() const = 0;

};
