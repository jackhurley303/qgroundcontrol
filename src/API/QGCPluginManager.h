/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

class QGCPlugin;

Q_DECLARE_LOGGING_CATEGORY(QGCPluginManagerLog)

/**
 * @class QGCPluginManager
 * @brief Manages runtime-loaded QGroundControl plugins
 *
 * This singleton class is responsible for discovering, loading, and managing
 * runtime plugins independently from the core QGC functionality. It provides
 * a clean separation between core plugin behavior (QGCCorePlugin) and runtime
 * plugin management.
 */
class QGCPluginManager : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("")
    Q_PROPERTY(QVariantList loadedPlugins READ loadedPlugins CONSTANT)
    Q_PROPERTY(QVariantList toolMenuItems READ toolMenuItems NOTIFY toolMenuItemsChanged)

public:
    explicit QGCPluginManager(QObject *parent = nullptr);
    ~QGCPluginManager() override;

    static QGCPluginManager *instance();
    static void registerQmlTypes();

    /// Initialize the plugin manager and load plugins
    void init();

    /// Cleanup all loaded plugins
    void cleanup();

    /// Get the list of loaded plugins (for QML)
    /// @return A list of loaded plugin info as QVariantList
    QVariantList loadedPlugins() const;

    /// Get the list of tool menu items from all loaded plugins
    /// @return A list of tool menu items
    QVariantList toolMenuItems() const { return _toolMenuItems; }

    /// Add a tool menu item from a loaded plugin
    /// @param item QVariantMap with keys: title, icon, source, visible
    void addToolMenuItem(const QVariantMap& item);

signals:
    /// Emitted when the tool menu items list changes
    void toolMenuItemsChanged();

private:
    void _loadPlugins();

    QVariantList _toolMenuItems;      // List of tool menu items (from plugins)
    QList<QGCPlugin*> _loadedPlugins; // List of loaded plugins
};
