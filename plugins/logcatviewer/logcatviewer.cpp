#include "logcatviewer.h"
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStyle>
#include <QTextStream>
#include <algorithm>
#include <QtConcurrent/QtConcurrent>

#include "taglabel.h"

LogcatViewer::LogcatViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_layout(new QVBoxLayout(m_container))
    , m_toolbar(new QToolBar())
    , m_tableView(new QTableView())
    , m_model(new LogcatTableModel(this))
    , m_tagComboBox(new QComboBox())
    , m_scrollArea(new QScrollArea())
    , m_tagsContainer(new QFrame())
    , m_tagsLayout(new QHBoxLayout(m_tagsContainer))
{
    setupUi();

    // Initialize level filters (all enabled by default)
    m_levelFilters[LogcatEntry::Level::Verbose] = true;
    m_levelFilters[LogcatEntry::Level::Debug] = true;
    m_levelFilters[LogcatEntry::Level::Info] = true;
    m_levelFilters[LogcatEntry::Level::Warning] = true;
    m_levelFilters[LogcatEntry::Level::Error] = true;
    m_levelFilters[LogcatEntry::Level::Fatal] = true;
    m_levelFilters[LogcatEntry::Level::Unknown] = true;
}

LogcatViewer::~LogcatViewer()
{
    delete m_container;
}

void LogcatViewer::setupUi()
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->addWidget(m_toolbar);

    // Setup toolbar with level filter buttons
    auto addLevelAction = [this](LogcatEntry::Level level) {
        QAction* action = new QAction(LogcatEntry::levelToString(level), this);
        action->setCheckable(true);
        action->setChecked(true);
        QColor color = LogcatEntry::levelColor(level);
        QPixmap pixmap(16, 16);
        pixmap.fill(color);
        action->setIcon(QIcon(pixmap));
        connect(action, &QAction::toggled, this, [this, level](bool checked) {
            toggleLevel(level, checked);
        });
        m_toolbar->addAction(action);
        m_levelActions[level] = action;
    };

    addLevelAction(LogcatEntry::Level::Verbose);
    addLevelAction(LogcatEntry::Level::Debug);
    addLevelAction(LogcatEntry::Level::Info);
    addLevelAction(LogcatEntry::Level::Warning);
    addLevelAction(LogcatEntry::Level::Error);
    addLevelAction(LogcatEntry::Level::Fatal);
    addLevelAction(LogcatEntry::Level::Unknown);

    // Setup tag combobox
    m_tagComboBox->setEditable(true);
    m_tagComboBox->setInsertPolicy(QComboBox::InsertAlphabetically);
    m_tagComboBox->setMinimumWidth(200);
    m_tagComboBox->setPlaceholderText(tr("Filter by package/tag..."));

    // Handle Enter key press in the combobox
    connect(m_tagComboBox->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
        QString tag = m_tagComboBox->currentText().trimmed();
        if (!tag.isEmpty() && !m_selectedTags.contains(tag)) {
            addTagLabel(tag);
            m_tagComboBox->setCurrentText("");
        }
    });

    // Handle item activation from dropdown
    connect(m_tagComboBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        QString tag = m_tagComboBox->itemText(index).trimmed();
        if (!tag.isEmpty() && !m_selectedTags.contains(tag)) {
            addTagLabel(tag);
            m_tagComboBox->setCurrentText("");
        }
    });

    // Setup scroll area for tags
    m_scrollArea->setWidget(m_tagsContainer);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFixedHeight(36);
    m_scrollArea->setFrameStyle(QFrame::NoFrame);

    // Setup tags container
    m_tagsContainer->setStyleSheet("QFrame { background: transparent; }");
    m_tagsLayout->setContentsMargins(0, 0, 0, 0);
    m_tagsLayout->setSpacing(2);
    m_tagsLayout->addStretch();

    // Add tag label, combobox and container to toolbar
    m_toolbar->addSeparator();
    QLabel* tagLabel = new QLabel(tr("Tags: "));
    m_toolbar->addWidget(tagLabel);
    m_toolbar->addWidget(m_tagComboBox);
    m_toolbar->addWidget(m_scrollArea);

    // Setup table view
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch); // Message column stretches
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(false);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSortingEnabled(true);
    m_tableView->setFont(QFont("Monospace"));
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    // Connect selection changes to our handler
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &LogcatViewer::onSelectionChanged);

    connect(m_tableView->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &LogcatViewer::handleSort);

    m_layout->addWidget(m_tableView);
}

void LogcatViewer::addTagLabel(const QString& tag)
{
    if (m_selectedTags.contains(tag)) {
        return;
    }

    m_selectedTags.insert(tag);
    TagLabel* label = new TagLabel(tag, m_tagsContainer);

    // Insert before the stretch
    m_tagsLayout->insertWidget(m_tagsLayout->count() - 1, label);

    connect(label, &TagLabel::removed, this, [this, label, tag]() {
        m_selectedTags.remove(tag);
        m_tagsLayout->removeWidget(label);
        label->deleteLater();
        updateVisibleRows();
    });

    updateVisibleRows();
}

bool LogcatViewer::loadContent(const QVector<LogEntry>& content)
{
    m_entries = content;
    m_model->setLogEntries(content);

    // Clear selected tags
    while (m_tagsLayout->count() > 1) { // Keep the stretch
        QLayoutItem* item = m_tagsLayout->takeAt(0);
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_selectedTags.clear();

    // Start background tag population
    QFuture<QSet<QString>> future = QtConcurrent::run([this]() {
        return getUniqueTags();
    });

    QFutureWatcher<QSet<QString>>* watcher = new QFutureWatcher<QSet<QString>>(this);
    connect(watcher, &QFutureWatcher<QSet<QString>>::finished, this, [this, watcher]() {
        QSet<QString> tagSet = watcher->result();
        QVector<QString> tags = tagSet.values();
        std::sort(tags.begin(), tags.end());
        
        m_tagComboBox->clear();
        for (const QString& tag : tags) {
            m_tagComboBox->addItem(tag);
        }
        
        watcher->deleteLater();
    });

    watcher->setFuture(future);
    return true;
}

void LogcatViewer::toggleLevel(LogcatEntry::Level level, bool enabled)
{
    m_levelFilters[level] = enabled;
    updateVisibleRows();
}

void LogcatViewer::updateVisibleRows()
{
    // First pass: find direct matches in parallel
    QVector<int> indices(m_entries.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    auto future = QtConcurrent::mapped(indices, [this](int i) {
        LogcatEntry entry(m_entries[i]);
        return matchesFilter(entry);
    });
    
    QVector<bool> directMatches = future.results();

    // Second pass: add matches and context lines
    QSet<int> linesToShow;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (directMatches[i]) {
            // Add context lines before
            for (int j = std::max(0, i - m_filterOptions.contextLinesBefore); j < i; ++j) {
                linesToShow.insert(j);
            }
            
            // Add the matching line
            linesToShow.insert(i);
            
            // Add context lines after
            for (int j = i + 1; j <= std::min<int>(m_entries.size() - 1, i + m_filterOptions.contextLinesAfter); ++j) {
                linesToShow.insert(j);
            }
        }
    }
    m_model->applyFilter(linesToShow.values().toVector());
}

void LogcatViewer::applyFilter(const FilterOptions& options)
{
    m_filterOptions = options;
    updateVisibleRows();
}

void LogcatViewer::handleSort(int column, Qt::SortOrder order)
{
    m_model->sort(column, order);
}

void LogcatViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(deselected);
    Q_UNUSED(selected);
    
    QList<int> selectedLines;
    const auto indexes = m_tableView->selectionModel()->selectedRows();
    for (const auto& index : indexes) {
        selectedLines.append(m_model->mapToSourceRow(index.row()));
    }
    
    emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(selectedLines));
}

void LogcatViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    Q_UNUSED(event);
    Q_UNUSED(data);
}

QSet<QString> LogcatViewer::getUniqueTags() const
{
    // Reduce function that safely combines tags into the result set
    auto reduceTags = [](QSet<QString>& result, const QString& tag) {
        if (!tag.isEmpty()) {
            result.insert(tag);
        }
        return result;
    };

    // Process entries and reduce results in parallel
    return QtConcurrent::blockingMappedReduced<QSet<QString>>(
        m_entries,
        // Map function to extract tags
        [this](const LogEntry& entry) {
            LogcatEntry logcatEntry(entry);
            return logcatEntry.tag;
        },
        // Reduce function to combine results
        reduceTags,
        // Use parallel reduction
        QtConcurrent::ReduceOption::UnorderedReduce
    );
}

bool LogcatViewer::matchesFilter(const LogcatEntry& entry) const
{
    // Check level filter
    if (!m_levelFilters.value(entry.level, true)) {
        return false;
    }

    // Check tag filters
    if (!m_selectedTags.isEmpty() && !m_selectedTags.contains(entry.tag)) {
        return false;
    }

    // Check text filter
    if (!m_filterOptions.query.isEmpty() && !entry.message.contains(m_filterOptions.query, m_filterOptions.caseSensitivity)) {
        return false;
    }

    return true;
}