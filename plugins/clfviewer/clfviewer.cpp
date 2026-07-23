#include "clfviewer.h"

#include <logdor/FormatRegistry.h>

CLFViewer::CLFViewer(QObject* parent)
    : PluginInterface(parent)
    , m_viewer(new LogViewerWidget())
{
    m_viewer->setParser(logdor::parserById(u"clf"));
    connect(m_viewer, &LogViewerWidget::linesSelected, this,
            [this](const QList<int>& lines) {
                emit pluginEvent(PluginEvent::LinesSelected,
                                 QVariant::fromValue(lines));
            });
}

CLFViewer::~CLFViewer()
{
    delete m_viewer;
}

void CLFViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                              std::shared_ptr<const logdor::LineIndex> index)
{
    m_viewer->setCoreSource(std::move(source), std::move(index));
}

bool CLFViewer::setLogs(const QList<LogEntry>& content)
{
    // Legacy path unused: this plugin is fed through setCoreSource().
    Q_UNUSED(content)
    return true;
}

void CLFViewer::setFilter(const FilterOptions& options)
{
    m_viewer->applyFilter(options);
}

QList<FieldInfo> CLFViewer::availableFields() const
{
    QList<FieldInfo> fields;
    const auto schema = m_viewer->model()->parser()->schema();
    for (const auto& field : schema) {
        DataType type = DataType::String;
        if (field.type == logdor::FieldType::Integer)
            type = DataType::Integer;
        else if (field.type == logdor::FieldType::DateTime)
            type = DataType::DateTime;
        fields.append({ field.name, type, {} });
    }
    return fields;
}

QSet<int> CLFViewer::filteredLines() const
{
    return QSet<int>();
}

void CLFViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    Q_UNUSED(lines)
}

void CLFViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected)
        m_viewer->selectSourceLines(data.value<QList<int>>());
}
