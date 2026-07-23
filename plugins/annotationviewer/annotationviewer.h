#ifndef ANNOTATIONVIEWER_H
#define ANNOTATIONVIEWER_H

#include "../../app/src/annotationhub.h"
#include "../../app/src/plugininterface.h"

#include <QLabel>
#include <QTreeWidget>
#include <QWidget>
#include <QtPlugin>

/**
 * The Annotations panel: every note on the open file in one list. Click a
 * note and every viewer jumps to its lines; edit or delete in place;
 * orphaned notes (anchor no longer found) are shown distinctly with a
 * re-anchor affordance. Replaces the old in-memory Bookmark Viewer.
 */
class AnnotationViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit AnnotationViewer(QObject* parent = nullptr);
    ~AnnotationViewer();

    QString name() const override { return tr("Annotations"); }
    QString version() const override { return "1.0.0"; }
    QString description() const override { return tr("All notes on the current log: jump, edit, share."); }
    QWidget* widget() override { return m_container; }

    // Core-aware (no data needed, but this keeps the panel off the legacy
    // materialization path when enabled).
    bool wantsCoreSource() const override { return true; }
    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override
    {
        Q_UNUSED(source)
        Q_UNUSED(index)
    }

    void setAnnotationHub(AnnotationHub* hub) override;

    bool setLogs(const QList<LogEntry>& content) override { Q_UNUSED(content) return true; }
    void setFilter(const FilterOptions& options) override { Q_UNUSED(options) }
    QList<FieldInfo> availableFields() const override { return {}; }
    QSet<int> filteredLines() const override { return {}; }
    void synchronizeFilteredLines(const QSet<int>& lines) override { Q_UNUSED(lines) }

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override
    {
        Q_UNUSED(event)
        Q_UNUSED(data)
    }

private slots:
    void rebuild();
    void onItemClicked(QTreeWidgetItem* item);
    void onItemActivated(QTreeWidgetItem* item);

private:
    void editAnnotation(const QUuid& id);
    void removeSelected();

    QWidget* m_container;
    QTreeWidget* m_tree;
    QLabel* m_summary;
    AnnotationHub* m_hub = nullptr;
};

#endif // ANNOTATIONVIEWER_H
