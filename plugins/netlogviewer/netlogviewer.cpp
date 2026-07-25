#include "netlogviewer.h"

#include <logdor/FormatRegistry.h>
#include <logdor/NetLogParser.h>

NetLogViewer::NetLogViewer(QObject* parent)
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
}

NetLogViewer::~NetLogViewer()
{
    delete m_viewer;
}

void NetLogViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                 std::shared_ptr<const logdor::LineIndex> index)
{
    std::shared_ptr<const logdor::NetLogParser> parser;
    if (source && index)
        parser = logdor::NetLogParser::fromFile(*source, *index);

    if (parser) {
        m_viewer->setParser(parser);
        // Only event lines carry data: hide the multi-MB constants line
        // (line 0) and the '"events": ['/closing-bracket wrapper lines,
        // keeping every '{'-led row - including the ok=false truncated tail
        // of a crashed-browser capture.
        m_viewer->setExtraPredicate(
            [](qint64 line, QByteArrayView raw) {
                return line != 0 && raw.trimmed().startsWith('{');
            },
            /*refilter=*/false);
    } else {
        // Not a net-export capture: plain text, nothing hidden.
        m_viewer->setParser(logdor::parserById(u"plaintext"));
        m_viewer->setExtraPredicate(nullptr, /*refilter=*/false);
    }

    m_viewer->setCoreSource(std::move(source), std::move(index));
    m_viewer->applyFilter(m_lastFilter);
}

void NetLogViewer::setFilter(const FilterOptions& options)
{
    m_lastFilter = options;
    m_viewer->applyFilter(options);
}

void NetLogViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
