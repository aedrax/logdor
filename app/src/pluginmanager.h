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

    /**
     * Create an extra instance of a multi-pane plugin (createInstance()),
     * registered under the lowest free "<name> N" key - names are stable
     * across runs so saved window layout and per-file view states
     * re-attach. The instance gets the cached annotation hub and highlight
     * rules and participates in every fan-out like a loaded plugin.
     * Returns nullptr when the plugin is unknown or single-instance;
     * otherwise the instance, with its registered name in *instanceName.
     */
    PluginInterface* createPaneInstance(const QString& baseName,
                                        QString* instanceName = nullptr);

    // Unregister and destroy an extra pane instance. No-op for loader
    // roots - only createPaneInstance() products are removable.
    void removePaneInstance(const QString& instanceName);

    // Get only enabled plugins
    QList<PluginInterface*> enabledPlugins() const;

    // Fan the core source out to enabled plugins. GUI thread only.
    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index);

    // Follow mode: fan a grown source/index out to enabled plugins.
    void extendCoreSource(std::shared_ptr<logdor::FileSource> source,
                          std::shared_ptr<const logdor::LineIndex> index,
                          qint64 firstNewLine);

    // Hand the shared annotation hub to ALL loaded plugins (pointer is
    // stable for the app lifetime, so disabled plugins get it too).
    void setAnnotationHub(AnnotationHub* hub);

    // Set filter for all enabled plugins
    void setFilter(const FilterOptions& options);

    // Fan the app-wide highlight rules out to ALL loaded plugins.
    void setHighlightRules(const QList<HighlightRule>& rules);

    // Shell-initiated event to every enabled plugin (no sender to exclude) -
    // e.g. selecting a line after a folder-search jump.
    void broadcastEvent(PluginEvent event, const QVariant& data);

    // Per-file view state, keyed by plugin name. Capture reads ALL loaded
    // plugins (disabled ones return empty objects, which are skipped);
    // restore fans out to enabled plugins only. GUI thread only.
    QJsonObject saveViewStates() const;
    void restoreViewStates(const QJsonObject& states);

private slots:
    // Handle and forward plugin events
    void onPluginEvent(PluginEvent event, const QVariant& data);

private:
    // Map of plugin name to loader
    QMap<QString, QPluginLoader*> m_pluginLoaders;

    // Every live instance by unique name: loader roots under their
    // plugin name, extra panes under "<name> N". This map - not
    // plugin->name(), which repeats across panes - is the identity used
    // for view-state keying and lookups.
    QMap<QString, PluginInterface*> m_plugins;

    // Caches so pane instances created later receive what loaded
    // plugins got at startup.
    AnnotationHub* m_annotationHub = nullptr;
    QList<HighlightRule> m_highlightRules;

    // Get the plugins directory path
    QString pluginsPath() const;

    // Load a specific plugin file
    bool loadPlugin(const QString& fileName);

    // Unload all plugins
    void unloadPlugins();

    // Forward event to all enabled plugins except the sender
    void forwardEventToPlugins(PluginEvent event, const QVariant& data,
                               PluginInterface* sender);
};

#endif // PLUGINMANAGER_H
