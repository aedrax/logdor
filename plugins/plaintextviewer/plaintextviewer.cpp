#include "plaintextviewer.h"

#include <logdor/FormatRegistry.h>

PlainTextViewer::PlainTextViewer(QObject* parent)
    : PluginInterface(parent)
    , m_viewer(new LogViewerWidget())
{
    m_viewer->setParser(logdor::parserById(u"plaintext"));
    connect(m_viewer, &LogViewerWidget::linesSelected, this,
            [this](const QList<int>& lines) {
                emit pluginEvent(PluginEvent::LinesSelected,
                                 QVariant::fromValue(lines));
            });
}

PlainTextViewer::~PlainTextViewer()
{
    delete m_viewer;
}

void PlainTextViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                    std::shared_ptr<const logdor::LineIndex> index)
{
    m_viewer->setCoreSource(std::move(source), std::move(index));
}

bool PlainTextViewer::setLogs(const QList<LogEntry>& content)
{
    // Legacy path unused: this plugin is fed through setCoreSource().
    Q_UNUSED(content)
    return true;
}

void PlainTextViewer::setFilter(const FilterOptions& options)
{
    m_viewer->applyFilter(options);
}

QList<FieldInfo> PlainTextViewer::availableFields() const
{
    return QList<FieldInfo>({
        { tr("No."), DataType::Integer },
        { tr("Log"), DataType::String },
    });
}

QSet<int> PlainTextViewer::filteredLines() const
{
    return QSet<int>();
}

void PlainTextViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    Q_UNUSED(lines)
}

void PlainTextViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected)
        m_viewer->selectSourceLines(data.value<QList<int>>());
}
