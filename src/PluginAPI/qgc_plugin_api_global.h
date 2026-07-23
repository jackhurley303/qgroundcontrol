/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QtGlobal>

/**
 * @defgroup PluginAPI QGC Plugin API
 * @brief The frozen ABI surface Tier SDK/internal plugins build against.
 *
 * Everything in this group ships in the SDK zip's `include/QGCPluginAPI/` and is
 * append-only once released: `QGCPluginInterface`/`QGCPlugin` are the plugin-side
 * vtables a plugin author implements; `QGCHostServices` and its per-domain
 * subclasses (`QGCMissionService`, `QGCParameterService`, `QGCReplayService`,
 * `QGCTelemetryLoggingService`, `QGCVehicleService`, `QGCAppService`) are the
 * host-side services a plugin consumes; `QGCReplayExtension` is a plugin-supplied
 * live extension object. See `plugins/README.md` for the manifest/tier model this
 * API sits under, and `plugins/template/SDK-README.md` for the plugin-author-facing
 * ABI compatibility contract.
 */

#if defined(QGCPLUGINAPI_LIBRARY)
#define QGCPLUGINAPI_EXPORT Q_DECL_EXPORT
#else
#define QGCPLUGINAPI_EXPORT Q_DECL_IMPORT
#endif
