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
#include <QtCore/QPointF>
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

    /// Returns a QML URL for a fly-view panel component provided by this plugin.
    /// Return an empty string (default) if the plugin does not provide a fly-view panel.
    virtual QString flyViewPanelUrl() const { return QString(); }

    /// Returns a QML URL for the collapsed dock item shown in the fly-view plugin strip.
    /// The loaded component fills the strip row and can show icons, badges, etc.
    /// Return an empty string (default) to use the built-in plain-name label.
    virtual QString flyViewPanelDockUrl() const { return QString(); }

    /// Default width of the fly-view floating panel in units of ScreenTools.defaultFontPixelWidth.
    /// Return 0 to use the framework default (30 font-width units).
    virtual double flyViewPanelDefaultWidth() const { return 0; }

    /// Default height of the fly-view floating panel in units of ScreenTools.defaultFontPixelHeight.
    /// Return 0 to use the framework default (15 font-height units).
    virtual double flyViewPanelDefaultHeight() const { return 0; }

    /// Default position of the fly-view floating panel as fractions [0, 1] of the parent size,
    /// where (0, 0) is the top-left corner and (1, 1) is the bottom-right corner.
    /// Return (-1, -1) to use the framework default (staggered from the right edge).
    virtual QPointF flyViewPanelDefaultPosition() const { return QPointF(-1, -1); }

    /// Get the plugin's display name
    /// @return Human-readable plugin name
    virtual QString name() const = 0;

};
