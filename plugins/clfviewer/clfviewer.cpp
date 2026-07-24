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

void CLFViewer::setFilter(const FilterOptions& options)
{
    m_viewer->applyFilter(options);
}

void CLFViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected)
        m_viewer->selectSourceLines(data.value<QList<int>>());
}
