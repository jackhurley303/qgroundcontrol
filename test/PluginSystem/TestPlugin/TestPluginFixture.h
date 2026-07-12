/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QGCPluginAPI/QGCPlugin.h>
#include <QGCPluginAPI/QGCPluginInterface.h>

#include <QtCore/QObject>
#include <QtCore/QtPlugin>

/// Minimal SDK-tier (Tier B) plugin, built and loaded only by
/// PluginLoaderGateTest — it links nothing but QGCPluginAPI + Qt, the same
/// shape as an out-of-tree plugin built against the published SDK. Never
/// auto-deployed to a runtime plugin directory (see the sibling CMakeLists.txt).
class TestPluginFixture : public QObject, public QGCPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGCPluginInterface_iid FILE "qgcplugin.json")
    Q_INTERFACES(QGCPluginInterface)

public:
    explicit TestPluginFixture(QObject* parent = nullptr);

    int pluginInterfaceVersion() const override { return QGCPluginApiVersion; }
    QGCPlugin* createPlugin(QObject* parent) override;
};

class TestPluginFixtureRuntime : public QGCPlugin
{
    Q_OBJECT

public:
    explicit TestPluginFixtureRuntime(QObject* parent = nullptr);
};
