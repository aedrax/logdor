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
    connect(m_viewer, &LogViewerWidget::filterTermRequested,
            this, &PluginInterface::filterTermRequested);
    connect(m_viewer, &LogViewerWidget::timeRangeRequested,
            this, &PluginInterface::timeRangeRequested);
    connect(m_viewer, &LogViewerWidget::highlightRequested,
            this, &PluginInterface::highlightRequested);
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

void CLFViewer::setFilter(const FilterOptions& options)
{
    m_viewer->applyFilter(options);
}

void CLFViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
