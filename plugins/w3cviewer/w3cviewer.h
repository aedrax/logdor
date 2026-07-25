#ifndef W3CVIEWER_H
#define W3CVIEWER_H

#include "../../app/src/logviewerwidget.h"
#include "../../app/src/plugininterface.h"

#include <QtPlugin>

class W3CViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit W3CViewer(QObject* parent = nullptr);
    ~W3CViewer();

    QString name() const override { return tr("W3C Log Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("Table view for W3C Extended Log Format files (IIS) with #Fields-derived columns."); }
    QWidget* widget() override { return m_viewer; }

    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override;

    void setAnnotationHub(AnnotationHub* hub) override
    {
        m_viewer->setAnnotationHub(hub);
    }

    void setFilter(const FilterOptions& options) override;

    void setHighlightRules(const QList<HighlightRule>& rules) override
    {
        m_viewer->setHighlightRules(rules);
    }

    QJsonObject saveViewState() const override
    {
        return m_viewer->saveViewState();
    }
    void restoreViewState(const QJsonObject& state) override
    {
        m_viewer->restoreViewState(state);
    }

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    LogViewerWidget* m_viewer;
    FilterOptions m_lastFilter;
};

#endif // W3CVIEWER_H
