#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include "plugininterface.h"
#include <QObject>
#include <QMap>
#include <QPluginLoader>

class PluginManager : public QObject {
    Q_OBJECT

public:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager();

    // Load all plugins from the plugins directory
    void loadPlugins();
    
    // Get a list of loaded plugin names
    QStringList pluginNames() const;
    
    // Get a plugin instance by name
    PluginInterface* plugin(const QString& name) const;
    
    // Get all loaded plugins
    QList<PluginInterface*> plugins() const;

private:
    // Map of plugin name to loader
    QMap<QString, QPluginLoader*> m_pluginLoaders;
    
    // Get the plugins directory path
    QString pluginsPath() const;
    
    // Load a specific plugin file
    bool loadPlugin(const QString& fileName);
    
    // Unload all plugins
    void unloadPlugins();
};

#endif // PLUGINMANAGER_H
