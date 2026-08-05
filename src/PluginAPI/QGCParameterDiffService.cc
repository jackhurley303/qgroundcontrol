/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCParameterDiffService.h"

// Out-of-line constructor/destructor anchor the vtable and metaobject in the
// SDK library so qobject_cast works across the plugin boundary.

QGCParameterDiffService::QGCParameterDiffService(QObject* parent) : QObject(parent) {}

QGCParameterDiffService::~QGCParameterDiffService() {}
