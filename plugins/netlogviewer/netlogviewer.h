#ifndef NETLOGVIEWER_H
#define NETLOGVIEWER_H

#include "../../app/src/logviewerwidget.h"
#include "../../app/src/plugininterface.h"

#include <QtPlugin>

class NetLogViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit NetLogViewer(QObject* parent = nullptr);
    ~NetLogViewer();

    QString name() const override { return tr("NetLog Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("Chrome net-export captures with decoded event and source types."); }
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

#endif // NETLOGVIEWER_H
