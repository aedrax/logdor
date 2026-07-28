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
    m_plugins[interface->name()] = interface;

    // Connect the new plugin's signals to the plugin manager
    connect(interface, &PluginInterface::pluginEvent,
            this, &PluginManager::onPluginEvent);

    return true;
}

PluginInterface* PluginManager::createPaneInstance(const QString& baseName,
                                                   QString* instanceName)
{
    PluginInterface* base = m_plugins.value(baseName);
    if (!base || !base->supportsMultiplePanes())
        return nullptr;
    PluginInterface* extra = base->createInstance();
    if (!extra)
        return nullptr;

    int n = 2;
    while (m_plugins.contains(QStringLiteral("%1 %2").arg(baseName).arg(n)))
        ++n;
    const QString name = QStringLiteral("%1 %2").arg(baseName).arg(n);

    extra->setParent(this); // deleted in unloadPlugins, before dlclose
    m_plugins[name] = extra;
    connect(extra, &PluginInterface::pluginEvent,
            this, &PluginManager::onPluginEvent);
    if (m_annotationHub)
        extra->setAnnotationHub(m_annotationHub);
    extra->setHighlightRules(m_highlightRules);

    if (instanceName)
        *instanceName = name;
    return extra;
}

void PluginManager::removePaneInstance(const QString& instanceName)
{
    if (m_pluginLoaders.contains(instanceName))
        return; // loader roots live until unloadPlugins
    if (PluginInterface* plugin = m_plugins.take(instanceName)) {
        disconnect(plugin, nullptr, this, nullptr);
        plugin->deleteLater();
    }
}

void PluginManager::unloadPlugins()
{
    // Pane instances first: their code lives in the plugin libraries, so
    // they must be gone before any loader unloads.
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (!m_pluginLoaders.contains(it.key()))
            delete it.value();
    }
    m_plugins.clear();
    for (QPluginLoader* loader : m_pluginLoaders) {
        loader->unload();
        delete loader;
    }
    m_pluginLoaders.clear();
}

QStringList PluginManager::pluginNames() const
{
    return m_plugins.keys();
}

PluginInterface* PluginManager::plugin(const QString& name) const
{
    return m_plugins.value(name);
}

QList<PluginInterface*> PluginManager::plugins() const
{
    return m_plugins.values();
}

QList<PluginInterface*> PluginManager::enabledPlugins() const
{
    QList<PluginInterface*> result;
    for (PluginInterface* plugin : m_plugins) {
        if (plugin->isEnabled())
            result.append(plugin);
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
    m_highlightRules = rules; // for pane instances created later
    for (PluginInterface* plugin : plugins())
        plugin->setHighlightRules(rules);
}

void PluginManager::setAnnotationHub(AnnotationHub* hub)
{
    m_annotationHub = hub; // for pane instances created later
    for (PluginInterface* plugin : plugins())
        plugin->setAnnotationHub(hub);
}void PluginManager::setFilter(const FilterOptions& options)
{
    for (PluginInterface* plugin : enabledPlugins()) {
        plugin->setFilter(options);
    }
}

QJsonObject PluginManager::saveViewStates() const
{
    // Keyed by registered name, not plugin->name(): every pane of a
    // multi-instance plugin reports the same name() but keeps its own
    // view state under "Log Viewer 2" etc.
    QJsonObject states;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        const QJsonObject state = it.value()->saveViewState();
        if (!state.isEmpty())
            states.insert(it.key(), state);
    }
    return states;
}

void PluginManager::restoreViewStates(const QJsonObject& states)
{
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (!it.value()->isEnabled())
            continue;
        const QJsonObject state = states.value(it.key()).toObject();
        if (!state.isEmpty())
            it.value()->restoreViewState(state);
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

void PluginManager::broadcastEvent(PluginEvent event, const QVariant& data)
{
    forwardEventToPlugins(event, data, nullptr);
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