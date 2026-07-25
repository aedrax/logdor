#include "w3cviewer.h"

#include <logdor/FormatRegistry.h>
#include <logdor/W3CExtendedParser.h>

W3CViewer::W3CViewer(QObject* parent)
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

W3CViewer::~W3CViewer()
{
    delete m_viewer;
}

void W3CViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                              std::shared_ptr<const logdor::LineIndex> index)
{
    std::shared_ptr<const logdor::W3CExtendedParser> parser;
    if (source && index)
        parser = logdor::W3CExtendedParser::fromFile(*source, *index);

    if (parser) {
        m_viewer->setParser(parser);
        // Directives provide the columns and can recur mid-file (IIS writes
        // a fresh block on service restart); hide them all by content.
        m_viewer->setExtraPredicate(
            [](qint64, QByteArrayView raw) {
                return !raw.trimmed().startsWith('#');
            },
            /*refilter=*/false);
    } else {
        // No #Fields directive: plain text, nothing hidden.
        m_viewer->setParser(logdor::parserById(u"plaintext"));
        m_viewer->setExtraPredicate(nullptr, /*refilter=*/false);
    }

    m_viewer->setCoreSource(std::move(source), std::move(index));
    m_viewer->applyFilter(m_lastFilter);
}

void W3CViewer::setFilter(const FilterOptions& options)
{
    m_lastFilter = options;
    m_viewer->applyFilter(options);
}

void W3CViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
