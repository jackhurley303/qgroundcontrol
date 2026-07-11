/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <memory>

#include <QtCore/QObject>

#include "QGCReplayExtension.h"
#include "qgc_plugin_api_global.h"

class QGCHostServices;
class QGCPluginPrivate;

/**
 * @class QGCPlugin
 * @brief Base class for runtime QGroundControl plugins
 *
 * This is the base class for all runtime-loaded plugins. Code is only for
 * behaviour: the lifecycle (init/cleanup) and live extension objects such as
 * the replay extension. Static contributions — tool menu entry, fly/plan-view
 * panels, the telemetry-logging claim — are declared as data in the plugin's
 * qgcplugin.json manifest ("contributes" object) and never queried from code.
 *
 * Unlike QGCCorePlugin (which is a singleton managing the core application),
 * QGCPlugin instances represent individual runtime plugins loaded from
 * shared libraries.
 */
class QGCPLUGINAPI_EXPORT QGCPlugin : public QObject
{
    Q_OBJECT

public:
    explicit QGCPlugin(QObject *parent = nullptr);
    ~QGCPlugin() override;

    /// Initialize the plugin
    /// Called once after the plugin is loaded and before it's used.
    /// @param host The host's service registry, or nullptr when the host
    /// provides no services. When non-null it stays valid for the plugin's
    /// lifetime; plugins that need it later store the pointer themselves.
    virtual void init(QGCHostServices* host) { Q_UNUSED(host); }

    /// Cleanup the plugin
    /// Called before the plugin is unloaded
    virtual void cleanup() { }

    /// Returns the plugin's flight replay extension, or nullptr if this plugin
    /// does not provide replay functionality. Only queried when the plugin's
    /// manifest declares "replay": true in its "contributes" object.
    virtual QGCReplayExtension* replayExtension() const { return nullptr; }

private:
    // ABI headroom: future state lives behind this pointer, never as new
    // data members of QGCPlugin itself.
    const std::unique_ptr<QGCPluginPrivate> _d;
};
