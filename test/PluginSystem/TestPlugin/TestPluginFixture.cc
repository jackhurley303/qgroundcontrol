/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TestPluginFixture.h"

TestPluginFixture::TestPluginFixture(QObject* parent)
    : QObject(parent)
{
}

QGCPlugin* TestPluginFixture::createPlugin(QObject* parent)
{
    return new TestPluginFixtureRuntime(parent);
}

TestPluginFixtureRuntime::TestPluginFixtureRuntime(QObject* parent)
    : QGCPlugin(parent)
{
}
