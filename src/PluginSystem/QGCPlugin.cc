/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCPlugin.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(QGCPluginLog, "PluginSystem.QGCPlugin")

QGCPlugin::QGCPlugin(QObject *parent)
    : QObject(parent)
{
}

QGCPlugin::~QGCPlugin()
{
}
