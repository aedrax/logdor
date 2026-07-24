#include "logcatviewer.h"

#include "../../app/src/logtablemodel.h"
#include "taglabel.h"

#include <logdor/FormatRegistry.h>
#include <logdor/LogcatParser.h>

#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QTableView>
#include <QtConcurrentRun>

namespace {

constexpr qint64 kTagSuggestionLineCap = 100'000;

const char* severityLabel(int severityIndex)
{
    switch (logdor::Severity(severityIndex)) {
    case logdor::Severity::Verbose: return "Verbose";
    case logdor::Severity::Debug: return "Debug";
    case logdor::Severity::Info: return "Info";
    case logdor::Severity::Warning: return "Warning";
    case logdor::Severity::Error: return "Error";
    case logdor::Severity::Fatal: return "Fatal";
    case logdor::Severity::None: break;
    }
    return "Unknown";
}

QColor severityIconColor(int severityIndex)
{
    const QColor color =
        LogTableModel::severityColor(logdor::Severity(severityIndex));
    return color.isValid() ? color : QColor(255, 255, 255); // Unknown = white
}

} // namespace

LogcatViewer::LogcatViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_layout(new QVBoxLayout(m_container))
    , m_toolbar(new QToolBar())
    , m_viewer(new LogViewerWidget())
    , m_parser(logdor::parserById(u"logcat"))
    , m_tagComboBox(new QComboBox())
    , m_scrollArea(new QScrollArea())
    , m_tagsContainer(new QFrame())
    , m_tagsLayout(new QHBoxLayout(m_tagsContainer))
{
    m_levelEnabled.fill(true);
    m_viewer->setParser(m_parser);
    setupUi();

    connect(m_viewer, &LogViewerWidget::linesSelected, this,
            [this](const QList<int>& lines) {
                emit pluginEvent(PluginEvent::LinesSelected,
                                 QVariant::fromValue(lines));
            });
    connect(&m_tagScanWatcher, &QFutureWatcherBase::finished, this, [this]() {
        if (m_tagScanWatcher.future().isCanceled()
            || m_tagScanWatcher.future().resultCount() == 0)
            return;
        const QString current = m_tagComboBox->currentText();
        m_tagComboBox->clear();
        m_tagComboBox->addItems(m_tagScanWatcher.future().result());
        m_tagComboBox->setCurrentText(current);
    });
}

LogcatViewer::~LogcatViewer()
{
    m_tagScanWatcher.cancel();
    delete m_container;
}

void LogcatViewer::setupUi()
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->addWidget(m_toolbar);

    auto createLevelIcon = [](const QColor& color, bool filtered) {
        QPixmap pixmap(16, 16);
        pixmap.fill(color);
        if (filtered) {
            QPainter painter(&pixmap);
            painter.setPen(QPen(Qt::white, 2));
            painter.drawLine(4, 4, 12, 12);
            painter.drawLine(12, 4, 4, 12);
        }
        return QIcon(pixmap);
    };

    auto addLevelAction = [this, createLevelIcon](int severityIndex) {
        QAction* action = new QAction(tr(severityLabel(severityIndex)), this);
        action->setCheckable(true);
        action->setChecked(true);
        const QColor color = severityIconColor(severityIndex);
        action->setIcon(createLevelIcon(color, false));
        connect(action, &QAction::toggled, this,
                [this, severityIndex, action, color, createLevelIcon](bool checked) {
                    action->setIcon(createLevelIcon(color, !checked));
                    m_levelEnabled[size_t(severityIndex)] = checked;
                    updatePredicate();
                });
        m_toolbar->addAction(action);
        m_levelActions[severityIndex] = action;
    };

    // Legacy button order: Verbose..Fatal then Unknown (= severity None).
    addLevelAction(int(logdor::Severity::Verbose));
    addLevelAction(int(logdor::Severity::Debug));
    addLevelAction(int(logdor::Severity::Info));
    addLevelAction(int(logdor::Severity::Warning));
    addLevelAction(int(logdor::Severity::Error));
    addLevelAction(int(logdor::Severity::Fatal));
    addLevelAction(int(logdor::Severity::None));

    m_tagComboBox->setEditable(true);
    m_tagComboBox->setInsertPolicy(QComboBox::InsertAlphabetically);
    m_tagComboBox->setMinimumWidth(200);
    m_tagComboBox->setPlaceholderText(tr("Filter by package/tag..."));

    connect(m_tagComboBox->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
        const QString tag = m_tagComboBox->currentText().trimmed();
        if (!tag.isEmpty() && !m_selectedTags.contains(tag)) {
            addTagLabel(tag);
            m_tagComboBox->setCurrentText("");
        }
    });
    connect(m_tagComboBox, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) {
                const QString tag = m_tagComboBox->itemText(index).trimmed();
                if (!tag.isEmpty() && !m_selectedTags.contains(tag)) {
                    addTagLabel(tag);
                    m_tagComboBox->setCurrentText("");
                }
            });

    m_scrollArea->setWidget(m_tagsContainer);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFixedHeight(36);
    m_scrollArea->setFrameStyle(QFrame::NoFrame);

    m_tagsContainer->setStyleSheet("QFrame { background: transparent; }");
    m_tagsLayout->setContentsMargins(0, 0, 0, 0);
    m_tagsLayout->setSpacing(2);
    m_tagsLayout->addStretch();

    m_toolbar->addSeparator();
    m_toolbar->addWidget(new QLabel(tr("Tags: ")));
    m_toolbar->addWidget(m_tagComboBox);
    m_toolbar->addWidget(m_scrollArea);

    m_viewer->tableView()->setAlternatingRowColors(false);
    m_layout->addWidget(m_viewer);
}

void LogcatViewer::addTagLabel(const QString& tag)
{
    if (m_selectedTags.contains(tag))
        return;

    m_selectedTags.insert(tag);
    TagLabel* label = new TagLabel(tag, m_tagsContainer);
    m_tagsLayout->insertWidget(m_tagsLayout->count() - 1, label);

    connect(label, &TagLabel::removed, this, [this, label, tag]() {
        m_selectedTags.remove(tag);
        m_tagsLayout->removeWidget(label);
        label->deleteLater();
        updatePredicate();
    });

    updatePredicate();
}

void LogcatViewer::updatePredicate()
{
    const bool allLevels = std::all_of(m_levelEnabled.begin(),
                                       m_levelEnabled.end(),
                                       [](bool b) { return b; });
    if (allLevels && m_selectedTags.isEmpty()) {
        // No chrome filter: null predicate keeps the empty-query passthrough.
        m_viewer->setExtraPredicate(nullptr);
        return;
    }

    // Captured by value; runs on scan worker threads (parser is stateless).
    m_viewer->setExtraPredicate(
        [levels = m_levelEnabled, tags = m_selectedTags, parser = m_parser](
            qint64, QByteArrayView raw) {
            logdor::ParsedRow row;
            parser->parseLine(raw, row);
            if (!levels[size_t(row.severity)])
                return false;
            return tags.isEmpty()
                || tags.contains(row.fields[logdor::LogcatParser::Tag]);
        });
}

void LogcatViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                 std::shared_ptr<const logdor::LineIndex> index)
{
    m_tagScanWatcher.cancel();
    m_source = std::move(source);
    m_index = std::move(index);
    m_viewer->setCoreSource(m_source, m_index);
    m_tagComboBox->clear();
    if (m_source && m_index)
        startTagSuggestionScan();
}

void LogcatViewer::startTagSuggestionScan()
{
    // Suggestions come from the first 100k lines, off-thread — full-file tag
    // enumeration contradicts the lazy architecture; any tag can still be
    // typed manually.
    auto source = m_source;
    auto index = m_index;
    auto parser = m_parser;
    m_tagScanWatcher.setFuture(QtConcurrent::run(
        [source, index, parser](QPromise<QStringList>& promise) {
            const qint64 cap = qMin(index->lineCount(), kTagSuggestionLineCap);
            QSet<QString> tags;
            logdor::ParsedRow row;
            for (qint64 line = 0; line < cap; ++line) {
                if ((line & 1023) == 0 && promise.isCanceled())
                    return;
                const QByteArray raw = source->read(index->offsetOf(line),
                                                    index->lengthOf(line));
                parser->parseLine(QByteArrayView(raw), row);
                if (row.ok && !row.fields[logdor::LogcatParser::Tag].isEmpty())
                    tags.insert(row.fields[logdor::LogcatParser::Tag]);
            }
            QStringList sorted(tags.begin(), tags.end());
            std::sort(sorted.begin(), sorted.end());
            promise.addResult(sorted);
        }));
}

void LogcatViewer::setFilter(const FilterOptions& options)
{
    m_viewer->applyFilter(options);
}

void LogcatViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
