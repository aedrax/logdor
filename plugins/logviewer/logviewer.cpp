#include "logviewer.h"

#include "../../app/src/formatcatalog.h"

#include <logdor/FormatRegistry.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

LogViewer::LogViewer(QObject* parent)
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
    rebuildFormatCombo();
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
    connect(m_viewer, &LogViewerWidget::filterTermRequested,
            this, &PluginInterface::filterTermRequested);
    connect(m_viewer, &LogViewerWidget::timeRangeRequested,
            this, &PluginInterface::timeRangeRequested);
    connect(m_viewer, &LogViewerWidget::highlightRequested,
            this, &PluginInterface::highlightRequested);
}

LogViewer::~LogViewer()
{
    delete m_container;
}

void LogViewer::rebuildFormatCombo()
{
    const QString previous = m_formatCombo->currentIndex() > 0
        ? m_formatCombo->currentText()
        : QString();
    // Alphabetical for the dropdown; detection uses the unsorted union
    // (applyFormatSelection), whose order is the specificity tiebreak.
    m_comboParsers = m_fileParsers + m_parsers;
    std::stable_sort(m_comboParsers.begin(), m_comboParsers.end(),
                     [](const auto& a, const auto& b) {
                         return QString::compare(a->displayName(),
                                                 b->displayName(),
                                                 Qt::CaseInsensitive) < 0;
                     });
    m_updatingCombo = true;
    m_formatCombo->clear();
    m_formatCombo->addItem(tr("Auto-detect"));
    for (const auto& parser : std::as_const(m_comboParsers))
        m_formatCombo->addItem(parser->displayName());
    // Keep the user's pick when the new file still offers it (by name, so
    // per-file parser instances don't matter); otherwise back to auto.
    const int keep = previous.isEmpty() ? -1 : m_formatCombo->findText(previous);
    m_formatCombo->setCurrentIndex(keep > 0 ? keep : 0);
    m_updatingCombo = false;
}

void LogViewer::setActiveParser(
    std::shared_ptr<const logdor::FormatParser> parser)
{
    if (parser->hasMetaLines()) {
        m_viewer->setExtraPredicate(
            [parser](qint64 line, QByteArrayView raw) {
                return parser->isDataLine(line, raw);
            },
            /*refilter=*/false);
    } else {
        m_viewer->setExtraPredicate(nullptr, /*refilter=*/false);
    }
    m_viewer->setParser(std::move(parser));
}

void LogViewer::applyFormatSelection(int comboIndex)
{
    std::shared_ptr<const logdor::FormatParser> parser;
    if (comboIndex == 0) {
        // Auto-detect against the current file; plaintext floor without one.
        if (m_source && m_index) {
            const auto detectParsers = m_fileParsers + m_parsers;
            const auto scores
                = logdor::detectFormat(*m_source, *m_index, detectParsers);
            if (!scores.isEmpty())
                parser = logdor::parserById(scores.front().parserId,
                                            detectParsers);
        }
        if (!parser)
            parser = logdor::parserById(u"plaintext", m_parsers);
        // Show what detection chose, e.g. "Auto-detect (Android Logcat)".
        m_formatCombo->setItemText(0, m_source && m_index
            ? tr("Auto-detect (%1)").arg(parser->displayName())
            : tr("Auto-detect"));
    } else if (comboIndex - 1 < m_comboParsers.size()) {
        parser = m_comboParsers[comboIndex - 1];
    }
    if (!parser)
        return;

    setActiveParser(parser);
    if (m_source && m_index)
        m_viewer->applyFilter(m_lastFilter);
}

void LogViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                    std::shared_ptr<const logdor::LineIndex> index)
{
    m_source = std::move(source);
    m_index = std::move(index);
    m_fileParsers = (m_source && m_index)
        ? logdor::fileDerivedParsers(*m_source, *m_index)
        : QList<std::shared_ptr<const logdor::FormatParser>>();
    rebuildFormatCombo();
    m_viewer->setCoreSource(m_source, m_index);
    // Re-resolve when auto-detecting or when a file-derived format is
    // selected (its parser is a fresh instance for this file); a static
    // pick needs nothing - the viewer already holds that parser.
    const int comboIndex = m_formatCombo->currentIndex();
    if (m_source && m_index
        && (comboIndex == 0 || comboIndex <= m_fileParsers.size()))
        applyFormatSelection(comboIndex);
}

void LogViewer::setFilter(const FilterOptions& options)
{
    m_lastFilter = options;
    m_viewer->applyFilter(options);
}

QJsonObject LogViewer::saveViewState() const
{
    QJsonObject state = m_viewer->saveViewState();
    // By display name, so catalog reordering can't select the wrong format;
    // auto-detect (index 0) stays the unsaved default.
    if (m_formatCombo->currentIndex() > 0)
        state.insert(QStringLiteral("format"), m_formatCombo->currentText());
    return state;
}

void LogViewer::restoreViewState(const QJsonObject& state)
{
    const QString format = state.value(QLatin1String("format")).toString();
    const int comboIndex = format.isEmpty() ? -1 : m_formatCombo->findText(format);
    if (comboIndex > 0 && comboIndex - 1 < m_comboParsers.size()) {
        m_updatingCombo = true;
        m_formatCombo->setCurrentIndex(comboIndex);
        m_updatingCombo = false;
        // Set the parser directly - the shell's setFilter() follows this
        // call and runs the scan, so applyFormatSelection()'s extra
        // re-filter would only waste a scan.
        setActiveParser(m_comboParsers[comboIndex - 1]);
    }
    m_viewer->restoreViewState(state);
}

void LogViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
