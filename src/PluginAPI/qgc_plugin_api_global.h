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

#if defined(QGCPLUGINAPI_LIBRARY)
#define QGCPLUGINAPI_EXPORT Q_DECL_EXPORT
#else
#define QGCPLUGINAPI_EXPORT Q_DECL_IMPORT
#endif
