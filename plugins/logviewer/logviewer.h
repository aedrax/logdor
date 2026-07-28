#ifndef LOGVIEWER_H
#define LOGVIEWER_H

#include "../../app/src/logviewerwidget.h"
#include "../../app/src/plugininterface.h"

#include <QComboBox>
#include <QWidget>
#include <QtPlugin>

class LogViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit LogViewer(QObject* parent = nullptr);
    ~LogViewer();

    QString name() const override { return tr("Log Viewer"); }
    QString version() const override { return "0.5.0"; }
    QString description() const override { return tr("The general-purpose viewer: renders any format, "
                                                     "built-in, user-defined, or file-derived (CSV, W3C, NetLog)."); }
    QWidget* widget() override { return m_container; }

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

    QJsonObject saveViewState() const override;
    void restoreViewState(const QJsonObject& state) override;

    // Extra panes let one file be viewed in two formats side by side.
    bool supportsMultiplePanes() const override { return true; }
    PluginInterface* createInstance() override { return new LogViewer(); }

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    void applyFormatSelection(int comboIndex);
    // Combo = Auto-detect, then this file's derived parsers (CSV/W3C/
    // NetLog), then the static catalog; keeps the selection by name.
    void rebuildFormatCombo();
    // Parser plus its meta-line predicate (header rows, directives).
    void setActiveParser(std::shared_ptr<const logdor::FormatParser> parser);

    QWidget* m_container;
    LogViewerWidget* m_viewer;
    QComboBox* m_formatCombo;
    QList<std::shared_ptr<const logdor::FormatParser>> m_parsers;
    QList<std::shared_ptr<const logdor::FormatParser>> m_fileParsers;
    QList<std::shared_ptr<const logdor::FormatParser>> m_comboParsers;
    FilterOptions m_lastFilter;
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    bool m_updatingCombo = false;
};

#endif // LOGVIEWER_H
