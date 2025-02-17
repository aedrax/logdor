#include "logcatviewer.h"
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStyle>
#include <QTextStream>
#include <algorithm>
#include <QtConcurrent/QtConcurrent>

TagLabel::TagLabel(const QString& tag, QWidget* parent)
    : QFrame(parent)
    , m_tag(tag)
    , m_layout(new QHBoxLayout(this))
    , m_label(new QLabel(tag))
    , m_removeButton(new QPushButton("✕"))
{
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    setStyleSheet("QFrame { background: #e0e0e0; border-radius: 4px; margin: 2px; }"
                  "QLabel { color: black; }");

    m_layout->setContentsMargins(6, 2, 2, 2);
    m_layout->setSpacing(4);

    m_removeButton->setFixedSize(16, 16);
    m_removeButton->setStyleSheet(
        "QPushButton { border: none; color: #666; background: transparent; padding: 0; }"
        "QPushButton:hover { color: #000; }");

    m_layout->addWidget(m_label);
    m_layout->addWidget(m_removeButton);

    connect(m_removeButton, &QPushButton::clicked, this, &TagLabel::removed);
}

LogcatTableModel::LogcatTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int LogcatTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleRows.size();
}

int LogcatTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 7; // No., Time, PID, TID, Level, Tag, Message
}

QVariant LogcatTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_visibleRows.size())
        return QVariant();

    const LogcatEntry& entry = logEntryToLogcatEntry(m_entries[m_visibleRows[index.row()]]);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return m_visibleRows[index.row()] + 1; // Line number (1-based)
        case 1:
            return entry.timestamp;
        case 2:
            return entry.pid;
        case 3:
            return entry.tid;
        case 4:
            return LogcatEntry::levelToString(entry.level);
        case 5:
            return entry.tag;
        case 6:
            return entry.message;
        }
    }
    else if (role == Qt::BackgroundRole) {
        return LogcatEntry::levelColor(entry.level);
    }
    else if (role == Qt::ForegroundRole) {
        return QColor(Qt::black);
    }
    else if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case 0:
        case 2:
        case 3:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

QVariant LogcatTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case 0: return tr("No.");
    case 1: return tr("Time");
    case 2: return tr("PID");
    case 3: return tr("TID");
    case 4: return tr("Level");
    case 5: return tr("Tag");
    case 6: return tr("Message");
    default: return QVariant();
    }
}

void LogcatTableModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    
    m_sortColumn = column;
    m_sortOrder = order;
    
    std::sort(m_visibleRows.begin(), m_visibleRows.end(), 
        [this, column, order](int left, int right) {
            const LogcatEntry& leftEntry = logEntryToLogcatEntry(m_entries[left]);
            const LogcatEntry& rightEntry = logEntryToLogcatEntry(m_entries[right]);
            
            bool lessThan;
            switch (column) {
            case 0: // Line number
                lessThan = left < right;
                break;
            case 1: // Time
                lessThan = leftEntry.timestamp < rightEntry.timestamp;
                break;
            case 2: // PID
                lessThan = leftEntry.pid.toLongLong() < rightEntry.pid.toLongLong();
                break;
            case 3: // TID
                lessThan = leftEntry.tid.toLongLong() < rightEntry.tid.toLongLong();
                break;
            case 4: // Level
                lessThan = leftEntry.level < rightEntry.level;
                break;
            case 5: // Tag
                lessThan = leftEntry.tag < rightEntry.tag;
                break;
            case 6: // Message
                lessThan = leftEntry.message < rightEntry.message;
                break;
            default:
                lessThan = false;
            }
            return order == Qt::AscendingOrder ? lessThan : !lessThan;
        });
    
    endResetModel();
}

void LogcatTableModel::setLogEntries(const QVector<LogEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    m_visibleRows.resize(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        m_visibleRows[i] = i;
    }
    endResetModel();
}

QSet<QString> LogcatTableModel::getUniqueTags() const
{
    auto extractTag = [this](const LogEntry& entry) -> QString {
        LogcatEntry logcatEntry = logEntryToLogcatEntry(entry);
        return logcatEntry.tag;
    };

    auto collectTags = [](QSet<QString>& tags, const QString& tag) {
        if (!tag.isEmpty()) {
            tags.insert(tag);
        }
    };

    QSet<QString> tags;
    QtConcurrent::blockingMappedReduced<QSet<QString>>(m_entries, extractTag, collectTags);
    return tags;
}

bool LogcatTableModel::matchesFilter(const LogcatEntry& entry, const QString& query, Qt::CaseSensitivity caseSensitivity,
                                   const QSet<QString>& tags,
                                   const QMap<LogcatEntry::Level, bool>& levelFilters) const
{
    // Check level filter
    if (!levelFilters.value(entry.level, true)) {
        return false;
    }

    // Check tag filters
    if (!tags.isEmpty() && !tags.contains(entry.tag)) {
        return false;
    }

    // Check text filter
    if (!query.isEmpty() && !entry.message.contains(query, caseSensitivity)) {
        return false;
    }

    return true;
}

void LogcatTableModel::applyFilter(const FilterOptions& filterOptions, const QSet<QString>& tags,
                                 const QMap<LogcatEntry::Level, bool>& levelFilters)
{
    beginResetModel();
    m_visibleRows.clear();

    // First pass: find direct matches in parallel
    QVector<int> indices(m_entries.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    auto future = QtConcurrent::mapped(indices, [this, &filterOptions, &tags, &levelFilters](int i) {
        LogcatEntry entry = logEntryToLogcatEntry(m_entries[i]);
        return matchesFilter(entry, filterOptions.query, filterOptions.caseSensitivity, tags, levelFilters);
    });
    
    QVector<bool> directMatches = future.results();

    // Second pass: add matches and context lines
    QSet<int> linesToShow;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (directMatches[i]) {
            // Add context lines before
            for (int j = std::max(0, i - filterOptions.contextLinesBefore); j < i; ++j) {
                linesToShow.insert(j);
            }
            
            // Add the matching line
            linesToShow.insert(i);
            
            // Add context lines after
            for (int j = i + 1; j <= std::min<int>(m_entries.size() - 1, i + filterOptions.contextLinesAfter); ++j) {
                linesToShow.insert(j);
            }
        }
    }

    m_visibleRows = linesToShow.values().toVector();
    std::sort(m_visibleRows.begin(), m_visibleRows.end());
    
    if (m_sortColumn >= 0) {
        sort(m_sortColumn, m_sortOrder);
    }
    
    endResetModel();
}

LogcatEntry LogcatTableModel::logEntryToLogcatEntry(const LogEntry& entry) const
{
    LogcatEntry logcatEntry;
    static QRegularExpression re(R"((\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+)\s+(\d+)\s+([VDIWEF])\s+([^:]+)\s*:\s*(.*))");
    auto match = re.match(entry.getMessage());
    if (match.hasMatch()) {
        logcatEntry.timestamp = match.captured(1);
        logcatEntry.pid = match.captured(2);
        logcatEntry.tid = match.captured(3);
        logcatEntry.level = LogcatEntry::parseLevel(match.captured(4)[0]);
        logcatEntry.tag = match.captured(5).trimmed();
        logcatEntry.message = match.captured(6);
    }
    return logcatEntry;
}

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

    // Add tag combobox and container to toolbar
    m_toolbar->addSeparator();
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
    m_model->setLogEntries(content);

    // Update tag combobox
    m_tagComboBox->clear();
    QSet<QString> tagSet = m_model->getUniqueTags();
    QVector<QString> tags = tagSet.values();
    std::sort(tags.begin(), tags.end());
    
    for (const QString& tag : tags) {
        m_tagComboBox->addItem(tag);
    }

    // Clear selected tags
    while (m_tagsLayout->count() > 1) { // Keep the stretch
        QLayoutItem* item = m_tagsLayout->takeAt(0);
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_selectedTags.clear();

    return true;
}

void LogcatViewer::toggleLevel(LogcatEntry::Level level, bool enabled)
{
    m_levelFilters[level] = enabled;
    updateVisibleRows();
}

void LogcatViewer::updateVisibleRows()
{
    m_model->applyFilter(m_filterOptions, m_selectedTags, m_levelFilters);
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
