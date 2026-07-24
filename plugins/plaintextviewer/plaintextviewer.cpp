#include "plaintextviewer.h"

#include "../../app/src/formatcatalog.h"

#include <logdor/FormatRegistry.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

PlainTextViewer::PlainTextViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_viewer(new LogViewerWidget())
    , m_formatCombo(new QComboBox())
    , m_parsers(loadAllParsers())
{
    auto* layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* formatBar = new QWidget();
    auto* formatLayout = new QHBoxLayout(formatBar);
    formatLayout->setContentsMargins(4, 2, 4, 2);
    formatLayout->addWidget(new QLabel(tr("Format:")));
    m_formatCombo->addItem(tr("Auto-detect"));
    for (const auto& parser : std::as_const(m_parsers))
        m_formatCombo->addItem(parser->displayName());
    formatLayout->addWidget(m_formatCombo);
    formatLayout->addStretch();
    layout->addWidget(formatBar);
    layout->addWidget(m_viewer);

    m_viewer->setParser(logdor::parserById(u"plaintext", m_parsers));

    connect(m_formatCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) {
                if (!m_updatingCombo)
                    applyFormatSelection(index);
            });
    connect(m_viewer, &LogViewerWidget::linesSelected, this,
            [this](const QList<int>& lines) {
                emit pluginEvent(PluginEvent::LinesSelected,
                                 QVariant::fromValue(lines));
            });
}

PlainTextViewer::~PlainTextViewer()
{
    delete m_container;
}

void PlainTextViewer::applyFormatSelection(int comboIndex)
{
    std::shared_ptr<const logdor::FormatParser> parser;
    if (comboIndex == 0) {
        // Auto-detect against the current file; plaintext floor without one.
        if (m_source && m_index) {
            const auto scores = logdor::detectFormat(*m_source, *m_index, m_parsers);
            if (!scores.isEmpty())
                parser = logdor::parserById(scores.front().parserId, m_parsers);
        }
        if (!parser)
            parser = logdor::parserById(u"plaintext", m_parsers);
    } else if (comboIndex - 1 < m_parsers.size()) {
        parser = m_parsers[comboIndex - 1];
    }
    if (!parser)
        return;

    m_viewer->setParser(parser);
    if (m_source && m_index)
        m_viewer->applyFilter(m_lastFilter);
}

void PlainTextViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                    std::shared_ptr<const logdor::LineIndex> index)
{
    m_source = std::move(source);
    m_index = std::move(index);
    m_viewer->setCoreSource(m_source, m_index);
    if (m_source && m_index && m_formatCombo->currentIndex() == 0)
        applyFormatSelection(0); // re-run auto-detection for the new file
}

void PlainTextViewer::setFilter(const FilterOptions& options)
{
    m_lastFilter = options;
    m_viewer->applyFilter(options);
}

QJsonObject PlainTextViewer::saveViewState() const
{
    QJsonObject state = m_viewer->saveViewState();
    // By display name, so catalog reordering can't select the wrong format;
    // auto-detect (index 0) stays the unsaved default.
    if (m_formatCombo->currentIndex() > 0)
        state.insert(QStringLiteral("format"), m_formatCombo->currentText());
    return state;
}

void PlainTextViewer::restoreViewState(const QJsonObject& state)
{
    const QString format = state.value(QLatin1String("format")).toString();
    const int comboIndex = format.isEmpty() ? -1 : m_formatCombo->findText(format);
    if (comboIndex > 0 && comboIndex - 1 < m_parsers.size()) {
        m_updatingCombo = true;
        m_formatCombo->setCurrentIndex(comboIndex);
        m_updatingCombo = false;
        // Set the parser directly - the shell's setFilter() follows this
        // call and runs the scan, so applyFormatSelection()'s extra
        // re-filter would only waste a scan.
        m_viewer->setParser(m_parsers[comboIndex - 1]);
    }
    m_viewer->restoreViewState(state);
}

void PlainTextViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
