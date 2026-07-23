#include "pluginmanager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <algorithm>

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
    , m_databaseManager(nullptr)
{
    // Initialize database manager
    m_databaseManager = new PluginDatabaseManager(this);
}

PluginManager::~PluginManager()
{
    cleanupDatabaseConnections();
    unloadPlugins();
}

void PluginManager::loadPlugins()
{
    QDir pluginsDir(pluginsPath());
    
    // Load only library files
    const QStringList filters = QStringList() << "*.so" << "*.dylib" << "*.dll";
    pluginsDir.setNameFilters(filters);
    
    // Try to load each plugin
    for (const QString& fileName : pluginsDir.entryList(QDir::Files)) {
        loadPlugin(pluginsDir.absoluteFilePath(fileName));
    }
}

QString PluginManager::pluginsPath() const
{
    QDir pluginsDir(QCoreApplication::applicationDirPath());
    qDebug() << "Application directory:" << pluginsDir.absolutePath();
    
    // Check if we're in a Debug/Release subdirectory
    if (pluginsDir.dirName() == "Debug" || pluginsDir.dirName() == "Release") {
        pluginsDir.cdUp();
    }
    
    // Look for plugins directory
    if (pluginsDir.cd("plugins")) {
        qDebug() << "Found plugins directory:" << pluginsDir.absolutePath();
        return pluginsDir.absolutePath();
    }
    
    // Try one level up
    pluginsDir.cdUp();
    if (pluginsDir.cd("plugins")) {
        qDebug() << "Found plugins directory one level up:" << pluginsDir.absolutePath();
        return pluginsDir.absolutePath();
    }
    
    qWarning() << "Could not find plugins directory, using application directory";
    return QCoreApplication::applicationDirPath();
}

bool PluginManager::loadPlugin(const QString& fileName)
{
    qDebug() << "Attempting to load plugin:" << fileName;
    
    QPluginLoader* loader = new QPluginLoader(fileName);
    
    // Check metadata before loading
    QJsonObject metadata = loader->metaData();
    if (metadata.isEmpty()) {
        qWarning() << "No metadata found in plugin:" << fileName;
        delete loader;
        return false;
    }
    qDebug() << "Plugin metadata:" << metadata;
    
    if (!loader->load()) {
        qWarning() << "Failed to load plugin:" << fileName;
        qWarning() << "Error:" << loader->errorString();
        delete loader;
        return false;
    }
    
    QObject* plugin = loader->instance();
    if (!plugin) {
        qWarning() << "Failed to create plugin instance:" << fileName;
        qWarning() << "Error:" << loader->errorString();
        delete loader;
        return false;
    }
    
    PluginInterface* interface = qobject_cast<PluginInterface*>(plugin);
    if (!interface) {
        qWarning() << "Plugin does not implement PluginInterface:" << fileName;
        qWarning() << "Plugin class name:" << plugin->metaObject()->className();
        loader->unload();
        delete loader;
        return false;
    }
    
    m_pluginLoaders[interface->name()] = loader;
    
    // Connect the new plugin's signals to the plugin manager
    connect(interface, &PluginInterface::pluginEvent,
            this, &PluginManager::onPluginEvent);
    
    return true;
}

void PluginManager::unloadPlugins()
{
    for (QPluginLoader* loader : m_pluginLoaders) {
        loader->unload();
        delete loader;
    }
    m_pluginLoaders.clear();
}

QStringList PluginManager::pluginNames() const
{
    return m_pluginLoaders.keys();
}

PluginInterface* PluginManager::plugin(const QString& name) const
{
    QPluginLoader* loader = m_pluginLoaders.value(name);
    if (!loader) {
        return nullptr;
    }
    
    QObject* instance = loader->instance();
    if (!instance) {
        return nullptr;
    }
    
    return qobject_cast<PluginInterface*>(instance);
}

QList<PluginInterface*> PluginManager::plugins() const
{
    QList<PluginInterface*> result;
    for (QPluginLoader* loader : m_pluginLoaders) {
        if (QObject* instance = loader->instance()) {
            if (PluginInterface* interface = qobject_cast<PluginInterface*>(instance)) {
                result.append(interface);
            }
        }
    }
    return result;
}

QList<PluginInterface*> PluginManager::enabledPlugins() const
{
    QList<PluginInterface*> result;
    for (QPluginLoader* loader : m_pluginLoaders) {
        if (QObject* instance = loader->instance()) {
            if (PluginInterface* interface = qobject_cast<PluginInterface*>(instance)) {
                if (interface->isEnabled()) {
                    result.append(interface);
                }
            }
        }
    }
    return result;
}

bool PluginManager::setLogs(const QList<LogEntry>& logs)
{
    // Call the overloaded version with empty file path for backward compatibility
    return setLogs(logs, QString());
}

bool PluginManager::setLogs(const QList<LogEntry>& logs, const QString& filePath)
{
    // Handle database context management for file-specific operations
    if (!filePath.isEmpty()) {
        if (shouldReinitializeDatabase(filePath)) {
            cleanupDatabaseConnections();
            m_currentFilePath = filePath;
            initializeDatabaseForPlugins(filePath);
            updateFileMetadata(filePath);
        }
    } else if (!m_currentFilePath.isEmpty()) {
        // If no file path provided but we had one before, clean up
        cleanupDatabaseConnections();
    }
    
    bool success = true;
    for (PluginInterface* plugin : enabledPlugins()) {
        // Core-source plugins were already served on the GUI thread; keeping
        // them out of this fan-out also keeps them off the worker thread when
        // the background-processing path calls in here.
        if (plugin->wantsCoreSource())
            continue;
        if (!plugin->setLogs(logs)) {
            qWarning() << "Failed to set logs for plugin:" << plugin->name();
            success = false;
        }
    }
    return success;
}

void PluginManager::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                  std::shared_ptr<const logdor::LineIndex> index)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    for (PluginInterface* plugin : enabledPlugins()) {
        if (plugin->wantsCoreSource())
            plugin->setCoreSource(source, index);
    }
}

bool PluginManager::anyEnabledLegacyPlugin() const
{
    const auto plugins = enabledPlugins();
    return std::any_of(plugins.begin(), plugins.end(),
                       [](PluginInterface* p) { return !p->wantsCoreSource(); });
}

void PluginManager::setFilter(const FilterOptions& options)
{
    for (PluginInterface* plugin : enabledPlugins()) {
        plugin->setFilter(options);
    }
}

void PluginManager::onPluginEvent(PluginEvent event, const QVariant& data)
{
    // Get the sender plugin
    PluginInterface* sender = qobject_cast<PluginInterface*>(QObject::sender());
    if (!sender) {
        qWarning() << "Received plugin event from unknown sender";
        return;
    }

    // Forward the event to other enabled plugins
    forwardEventToPlugins(event, data, sender);
}

void PluginManager::forwardEventToPlugins(PluginEvent event, const QVariant& data, PluginInterface* sender)
{
    for (PluginInterface* plugin : enabledPlugins()) {
        // Don't forward event back to sender
        if (plugin != sender) {
            plugin->onPluginEvent(event, data);
        }
    }
}

void PluginManager::initializeDatabaseForPlugins(const QString& filePath)
{
    if (!m_databaseManager || filePath.isEmpty()) {
        return;
    }
    
    qDebug() << "Initializing database for file:" << filePath;
    
    // Initialize database for the file
    if (!m_databaseManager->initializeForFile(filePath)) {
        qWarning() << "Failed to initialize database for file:" << filePath;
        qWarning() << "Database error:" << m_databaseManager->lastError();
        return;
    }
    
    // Assign database manager to database-capable plugins
    assignDatabaseToPlugins();
}

void PluginManager::cleanupDatabaseConnections()
{
    if (!m_databaseManager) {
        return;
    }
    
    qDebug() << "Cleaning up database connections";
    
    // Remove database manager from all plugins
    for (PluginInterface* plugin : plugins()) {
        if (plugin->supportsDatabaseStorage()) {
            plugin->setDatabaseManager(nullptr);
        }
    }
    
    // Close database connection
    m_databaseManager->closeDatabase();
    m_currentFilePath.clear();
}

void PluginManager::assignDatabaseToPlugins()
{
    if (!m_databaseManager || !m_databaseManager->isReady()) {
        return;
    }
    
    qDebug() << "Assigning database manager to database-capable plugins";
    
    for (PluginInterface* plugin : plugins()) {
        if (plugin->supportsDatabaseStorage()) {
            qDebug() << "Assigning database to plugin:" << plugin->name();
            
            if (!plugin->setDatabaseManager(m_databaseManager)) {
                qWarning() << "Failed to assign database manager to plugin:" << plugin->name();
                continue;
            }
            
            // Create plugin table if it doesn't exist
            QList<FieldInfo> schema = plugin->getDatabaseSchema();
            if (!schema.isEmpty()) {
                if (!m_databaseManager->createPluginTable(plugin->name(), schema)) {
                    qWarning() << "Failed to create database table for plugin:" << plugin->name();
                    qWarning() << "Database error:" << m_databaseManager->lastError();
                }
            }
        }
    }
}

bool PluginManager::isDatabaseInitializedForFile(const QString& filePath) const
{
    if (!m_databaseManager || filePath.isEmpty()) {
        return false;
    }
    
    return m_currentFilePath == filePath && m_databaseManager->isReady();
}

void PluginManager::clearDatabaseCache()
{
    qDebug() << "Clearing database cache";
    cleanupDatabaseConnections();
}

bool PluginManager::shouldReinitializeDatabase(const QString& filePath) const
{
    // Reinitialize if:
    // 1. No current file path set
    // 2. Different file path
    // 3. Database not ready
    // 4. Database manager not available
    
    if (filePath.isEmpty()) {
        return false;
    }
    
    if (m_currentFilePath.isEmpty()) {
        return true;
    }
    
    if (filePath != m_currentFilePath) {
        return true;
    }
    
    if (!m_databaseManager || !m_databaseManager->isReady()) {
        return true;
    }
    
    return false;
}

void PluginManager::updateFileMetadata(const QString& filePath)
{
    if (!m_databaseManager || !m_databaseManager->isReady() || filePath.isEmpty()) {
        return;
    }
    
    qDebug() << "Updating file metadata for:" << filePath;
    
    // Get file information
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qWarning() << "File does not exist:" << filePath;
        return;
    }
    
    qint64 fileSize = fileInfo.size();
    QDateTime lastModified = fileInfo.lastModified();
    
    // Update file metadata in database
    // This would typically be done through the DatabaseManager
    // For now, we'll just log the information
    qDebug() << "File size:" << fileSize << "bytes";
    qDebug() << "Last modified:" << lastModified.toString(Qt::ISODate);
}
