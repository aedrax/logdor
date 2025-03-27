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

    // Get only enabled plugins
    QList<PluginInterface*> enabledPlugins() const;

    // Set logs for all enabled plugins
    bool setLogs(const QList<LogEntry>& logs);

    // Set filter for all enabled plugins
    void setFilter(const FilterOptions& options);

private slots:
    // Handle and forward plugin events
    void onPluginEvent(PluginEvent event, const QVariant& data);

private:
    // Map of plugin name to loader
    QMap<QString, QPluginLoader*> m_pluginLoaders;
    
    // Get the plugins directory path
    QString pluginsPath() const;
    
    // Load a specific plugin file
    bool loadPlugin(const QString& fileName);
    
    // Unload all plugins
    void unloadPlugins();
    
    // Forward event to all enabled plugins except the sender
    void forwardEventToPlugins(PluginEvent event, const QVariant& data, PluginInterface* sender);
};

#endif // PLUGINMANAGER_H
