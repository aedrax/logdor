#include "pluginmanager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager()
{
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
