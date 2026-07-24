#ifndef PLUGININTERFACE_H
#define PLUGININTERFACE_H

#include <QString>
#include <QWidget>
#include <QtPlugin>

#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>

#include <memory>

class AnnotationHub;

/// State of the shared filter bar, fanned out to every enabled plugin.
struct FilterOptions {
    QString query;
    int contextLinesBefore = 0;
    int contextLinesAfter = 0;
    Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
    bool invertFilter = false;
    bool inQueryMode = false; // field query language (CompiledQuery)
    bool inRegexMode = false;

    FilterOptions(const QString& q = QString(), int before = 0, int after = 0,
                  Qt::CaseSensitivity cs = Qt::CaseInsensitive,
                  bool invert = false, bool queryMode = false,
                  bool regexMode = false)
        : query(q)
        , contextLinesBefore(before)
        , contextLinesAfter(after)
        , caseSensitivity(cs)
        , invertFilter(invert)
        , inQueryMode(queryMode)
        , inRegexMode(regexMode)
    {
    }
};

/// Cross-plugin events, fanned out synchronously by PluginManager.
enum class PluginEvent {
    LinesSelected, // payload: QList<int> of SOURCE line numbers

    /**
     * Restrict every viewer to a set of source lines — e.g. the Map Viewer
     * broadcasting the lines inside a drawn area. Payload: sorted
     * QList<int>; an EMPTY list lifts the restriction. Viewers AND this
     * with their own filters; a new file clears it implicitly.
     */
    LinesConstrained,
};

/**
 * A Logdor view plugin. Everything is delivered on the GUI thread; plugins
 * run their own background work (filter scans etc.) via QFutureWatcher —
 * see LogViewerWidget, which most viewers simply wrap.
 */
class Q_DECL_EXPORT PluginInterface : public QObject {
    Q_OBJECT
public:
    explicit PluginInterface(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~PluginInterface() override = default;

    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;

    /// The dock widget content. The plugin keeps ownership.
    virtual QWidget* widget() = 0;

    /**
     * The opened file. Both null => file closed or being replaced: drop
     * every reference now, the old mapping dies after this returns.
     */
    virtual void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                               std::shared_ptr<const logdor::LineIndex> index)
    {
        Q_UNUSED(source)
        Q_UNUSED(index)
    }

    /**
     * Shared annotation state (notes on lines/ranges). Called once at
     * startup; the pointer stays valid for the application lifetime.
     */
    virtual void setAnnotationHub(AnnotationHub* hub) { Q_UNUSED(hub) }

    /// The shared filter bar changed.
    virtual void setFilter(const FilterOptions& options) { Q_UNUSED(options) }

    void setEnabled(bool enabled)
    {
        if (m_enabled != enabled)
            m_enabled = enabled;
    }

    bool isEnabled() const { return m_enabled; }

public slots:
    /// Events from other plugins (never echoed back to the sender).
    virtual void onPluginEvent(PluginEvent event, const QVariant& data)
    {
        Q_UNUSED(event)
        Q_UNUSED(data)
    }

signals:
    /// Broadcast an event to every other enabled plugin.
    void pluginEvent(PluginEvent event, const QVariant& data);

protected:
    bool m_enabled = true;
};

// /3.0: legacy setLogs pipeline, field metadata, and database API removed;
// stale binaries must fail to load.
#define PluginInterface_iid "com.logdor.PluginInterface/3.0"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)

#endif // PLUGININTERFACE_H
