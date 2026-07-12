/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCPlugin.h"

class QGCPluginPrivate
{
};

QGCPlugin::QGCPlugin(QObject *parent)
    : QObject(parent)
    , _d(std::make_unique<QGCPluginPrivate>())
{
}

QGCPlugin::~QGCPlugin()
{
}

// Default virtual bodies live in the SDK library, not inline: a plugin that
// doesn't override them binds the symbol here, so the SDK can still evolve the
// default behaviour for already-shipped plugins (an ABI-safe evolution channel).

void QGCPlugin::init(QGCHostServices *host)
{
    Q_UNUSED(host);
}

void QGCPlugin::cleanup()
{
}

QGCReplayExtension *QGCPlugin::replayExtension() const
{
    return nullptr;
}
