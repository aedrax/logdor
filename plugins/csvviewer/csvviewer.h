#ifndef CSVVIEWER_H
#define CSVVIEWER_H

#include "../../app/src/logviewerwidget.h"
#include "../../app/src/plugininterface.h"

#include <QtPlugin>

class CsvViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit CsvViewer(QObject* parent = nullptr);
    ~CsvViewer();

    QString name() const override { return tr("CSV Viewer"); }
    QString version() const override { return "0.2.0"; }
    QString description() const override { return tr("Table view for CSV files with header-derived columns."); }
    QWidget* widget() override { return m_viewer; }

    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override;

    void setAnnotationHub(AnnotationHub* hub) override
    {
        m_viewer->setAnnotationHub(hub);
    }

    void setFilter(const FilterOptions& options) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    LogViewerWidget* m_viewer;
    FilterOptions m_lastFilter;
};

#endif // CSVVIEWER_H
