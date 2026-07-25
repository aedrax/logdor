#include "csvviewer.h"

#include <logdor/CsvParser.h>
#include <logdor/FormatRegistry.h>

CsvViewer::CsvViewer(QObject* parent)
    : PluginInterface(parent)
    , m_viewer(new LogViewerWidget())
{
    m_viewer->setParser(logdor::parserById(u"plaintext"));
    connect(m_viewer, &LogViewerWidget::linesSelected, this,
            [this](const QList<int>& lines) {
                emit pluginEvent(PluginEvent::LinesSelected,
                                 QVariant::fromValue(lines));
            });
    connect(m_viewer, &LogViewerWidget::filterTermRequested,
            this, &PluginInterface::filterTermRequested);
    connect(m_viewer, &LogViewerWidget::timeRangeRequested,
            this, &PluginInterface::timeRangeRequested);
    connect(m_viewer, &LogViewerWidget::highlightRequested,
            this, &PluginInterface::highlightRequested);
}

CsvViewer::~CsvViewer()
{
    delete m_viewer;
}

void CsvViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                              std::shared_ptr<const logdor::LineIndex> index)
{
    std::shared_ptr<const logdor::CsvParser> parser;
    if (source && index)
        parser = logdor::CsvParser::fromFile(*source, *index);

    if (parser) {
        m_viewer->setParser(parser);
        // The header row provides the columns; don't show it as data.
        m_viewer->setExtraPredicate(
            [](qint64 line, QByteArrayView) { return line != 0; },
            /*refilter=*/false);
    } else {
        // Empty/headerless file: plain text, nothing hidden.
        m_viewer->setParser(logdor::parserById(u"plaintext"));
        m_viewer->setExtraPredicate(nullptr, /*refilter=*/false);
    }

    m_viewer->setCoreSource(std::move(source), std::move(index));
    m_viewer->applyFilter(m_lastFilter);
}

void CsvViewer::setFilter(const FilterOptions& options)
{
    m_lastFilter = options;
    m_viewer->applyFilter(options);
}

void CsvViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected) {
        m_viewer->selectSourceLines(data.value<QList<int>>());
    } else if (event == PluginEvent::LinesConstrained) {
        const QList<int> lines = data.value<QList<int>>();
        auto sorted = std::make_shared<std::vector<qint32>>(lines.begin(),
                                                            lines.end());
        m_viewer->setLineConstraint(std::move(sorted));
    }
}
