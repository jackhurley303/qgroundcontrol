#pragma once

#include "PluginSystem/QGCPlugin.h"
#include "PluginSystem/QGCPluginInterface.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QtPlugin>

Q_DECLARE_LOGGING_CATEGORY(ExamplePluginLog)

/**
 * @class ExamplePlugin
 * @brief Example plugin demonstrating the QGC plugin architecture
 *
 * This is a minimal plugin that adds a custom tool menu item.
 */
class ExamplePlugin : public QObject, public QGCPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.mavlink.qgroundcontrol.QGCPluginInterface")
    Q_INTERFACES(QGCPluginInterface)

public:
    explicit ExamplePlugin(QObject* parent = nullptr);
    ~ExamplePlugin() override = default;

    // QGCPluginInterface interface
    int pluginInterfaceVersion() const override { return 1; }
    QGCPlugin* createPlugin(QObject* parent) override;
};

/**
 * @class ExampleRuntimePlugin
 * @brief Runtime plugin implementation for the example plugin
 */
class ExampleRuntimePlugin : public QGCPlugin
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ExampleRuntimePlugin(QObject* parent = nullptr);
    ~ExampleRuntimePlugin() override = default;

    // QGCPlugin interface
    QString name() const override { return "Example"; }
    QVariantMap toolMenuItem() const override;

private:
    QVariantMap _toolMenuItem;
};
