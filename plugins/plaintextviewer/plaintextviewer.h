#ifndef PLAINTEXTVIEWER_H
#define PLAINTEXTVIEWER_H

#include "../../app/src/logviewerwidget.h"
#include "../../app/src/plugininterface.h"

#include <QComboBox>
#include <QWidget>
#include <QtPlugin>

class PlainTextViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit PlainTextViewer(QObject* parent = nullptr);
    ~PlainTextViewer();

    QString name() const override { return tr("Plain Text Viewer"); }
    QString version() const override { return "0.3.0"; }
    QString description() const override { return tr("A viewer for any text log, with selectable formats."); }
    QWidget* widget() override { return m_container; }

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
    void applyFormatSelection(int comboIndex);

    QWidget* m_container;
    LogViewerWidget* m_viewer;
    QComboBox* m_formatCombo;
    QList<std::shared_ptr<const logdor::FormatParser>> m_parsers;
    FilterOptions m_lastFilter;
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    bool m_updatingCombo = false;
};

#endif // PLAINTEXTVIEWER_H
