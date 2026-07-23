#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include "plugininterface.h"
#include "plugindatabasemanager.h"
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
    
    // Set logs with file path for database initialization
    bool setLogs(const QList<LogEntry>& logs, const QString& filePath);

    // Fan the core source out to enabled wantsCoreSource() plugins.
    // GUI thread only.
    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index);

    // True when any enabled plugin still needs the legacy QList<LogEntry>.
    bool anyEnabledLegacyPlugin() const;

    // Hand the shared annotation hub to ALL loaded plugins (pointer is
    // stable for the app lifetime, so disabled plugins get it too).
    void setAnnotationHub(AnnotationHub* hub);

    // Set filter for all enabled plugins
    void setFilter(const FilterOptions& options);
    
    // Database management
    PluginDatabaseManager* getDatabaseManager() const { return m_databaseManager; }
    QString getCurrentFilePath() const { return m_currentFilePath; }
    
    // Database context management
    bool isDatabaseInitializedForFile(const QString& filePath) const;
    void clearDatabaseCache();

private slots:
    // Handle and forward plugin events
    void onPluginEvent(PluginEvent event, const QVariant& data);

private:
    // Map of plugin name to loader
    QMap<QString, QPluginLoader*> m_pluginLoaders;
    
    // Database manager for plugin data storage
    PluginDatabaseManager* m_databaseManager;
    
    // Current file path for database context
    QString m_currentFilePath;
    
    // Get the plugins directory path
    QString pluginsPath() const;
    
    // Load a specific plugin file
    bool loadPlugin(const QString& fileName);
    
    // Unload all plugins
    void unloadPlugins();
    
    // Forward event to all enabled plugins except the sender
    void forwardEventToPlugins(PluginEvent event, const QVariant& data, PluginInterface* sender);
    
    // Database integration helpers
    void initializeDatabaseForPlugins(const QString& filePath);
    void cleanupDatabaseConnections();
    void assignDatabaseToPlugins();
    
    // File-specific database context management
    bool shouldReinitializeDatabase(const QString& filePath) const;
    void updateFileMetadata(const QString& filePath);
};

#endif // PLUGINMANAGER_H
