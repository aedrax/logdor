#include "pluginmanager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <algorithm>

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
}void PluginManager::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                  std::shared_ptr<const logdor::LineIndex> index)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    for (PluginInterface* plugin : enabledPlugins())
        plugin->setCoreSource(source, index);
}

void PluginManager::extendCoreSource(std::shared_ptr<logdor::FileSource> source,
                                     std::shared_ptr<const logdor::LineIndex> index,
                                     qint64 firstNewLine)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    for (PluginInterface* plugin : enabledPlugins())
        plugin->coreSourceExtended(source, index, firstNewLine);
}

void PluginManager::setHighlightRules(const QList<HighlightRule>& rules)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    for (PluginInterface* plugin : plugins())
        plugin->setHighlightRules(rules);
}

void PluginManager::setAnnotationHub(AnnotationHub* hub)
{
    for (QPluginLoader* loader : m_pluginLoaders) {
        if (QObject* instance = loader->instance()) {
            if (auto* plugin = qobject_cast<PluginInterface*>(instance))
                plugin->setAnnotationHub(hub);
        }
    }
}void PluginManager::setFilter(const FilterOptions& options)
{
    for (PluginInterface* plugin : enabledPlugins()) {
        plugin->setFilter(options);
    }
}

QJsonObject PluginManager::saveViewStates() const
{
    QJsonObject states;
    for (PluginInterface* plugin : plugins()) {
        const QJsonObject state = plugin->saveViewState();
        if (!state.isEmpty())
            states.insert(plugin->name(), state);
    }
    return states;
}

void PluginManager::restoreViewStates(const QJsonObject& states)
{
    for (PluginInterface* plugin : enabledPlugins()) {
        const QJsonObject state = states.value(plugin->name()).toObject();
        if (!state.isEmpty())
            plugin->restoreViewState(state);
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