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

QList<FieldInfo> CsvViewer::availableFields() const
{
    QList<FieldInfo> fields { { tr("No."), DataType::Integer, {} } };
    if (const auto& parser = m_viewer->model()->parser()) {
        for (const auto& field : parser->schema())
            fields.append({ field.name,
                            field.type == logdor::FieldType::Integer
                                ? DataType::Integer
                                : DataType::String,
                            {} });
    }
    return fields;
}

void CsvViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected)
        m_viewer->selectSourceLines(data.value<QList<int>>());
}
