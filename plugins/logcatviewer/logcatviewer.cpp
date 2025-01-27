#include "logcatviewer.h"
#include <QHeaderView>
#include <QTextStream>
#include <QRegularExpression>
#include <QIcon>
#include <QStyle>
#include <QLineEdit>

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
        "QPushButton:hover { color: #000; }"
    );
    
    m_layout->addWidget(m_label);
    m_layout->addWidget(m_removeButton);
    
    connect(m_removeButton, &QPushButton::clicked, this, &TagLabel::removed);
}

LogcatViewer::LogcatViewer()
    : QObject()
    , m_container(new QWidget())
    , m_layout(new QVBoxLayout(m_container))
    , m_toolbar(new QToolBar())
    , m_table(new QTableWidget())
    , m_tagComboBox(new QComboBox())
    , m_scrollArea(new QScrollArea())
    , m_tagsContainer(new QFrame())
    , m_tagsLayout(new QHBoxLayout(m_tagsContainer))
{
    m_tagComboBox->setEditable(true);
    m_tagComboBox->setInsertPolicy(QComboBox::InsertAlphabetically);
    m_tagComboBox->setMinimumWidth(200);
    m_tagComboBox->setPlaceholderText("Filter by package/tag...");
    
    // Setup scroll area for tags
    m_scrollArea->setWidget(m_tagsContainer);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFixedHeight(36); // Enough height for one row of tags
    m_scrollArea->setFrameStyle(QFrame::NoFrame);
    
    // Setup tags container
    m_tagsContainer->setStyleSheet("QFrame { background: transparent; }");
    m_tagsLayout->setContentsMargins(0, 0, 0, 0);
    m_tagsLayout->setSpacing(2);
    m_tagsLayout->addStretch();
    
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
    
    setupUi();
    
    // Initialize level filters (all enabled by default)
    m_levelFilters[LogEntry::Level::Verbose] = true;
    m_levelFilters[LogEntry::Level::Debug] = true;
    m_levelFilters[LogEntry::Level::Info] = true;
    m_levelFilters[LogEntry::Level::Warning] = true;
    m_levelFilters[LogEntry::Level::Error] = true;
    m_levelFilters[LogEntry::Level::Fatal] = true;
}

LogcatViewer::~LogcatViewer()
{
    delete m_container;
}

void LogcatViewer::setupUi()
{
    // Setup toolbar with level filter buttons
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->addWidget(m_toolbar);
    
    auto addLevelAction = [this](LogEntry::Level level) {
        QAction* action = new QAction(LogEntry::levelToString(level), this);
        action->setCheckable(true);
        action->setChecked(true);
        QColor color = LogEntry::levelColor(level);
        QPixmap pixmap(16, 16);
        pixmap.fill(color);
        action->setIcon(QIcon(pixmap));
        connect(action, &QAction::toggled, this, [this, level](bool checked) {
            toggleLevel(level, checked);
        });
        m_toolbar->addAction(action);
        m_levelActions[level] = action;
    };

    addLevelAction(LogEntry::Level::Verbose);
    addLevelAction(LogEntry::Level::Debug);
    addLevelAction(LogEntry::Level::Info);
    addLevelAction(LogEntry::Level::Warning);
    addLevelAction(LogEntry::Level::Error);
    addLevelAction(LogEntry::Level::Fatal);

    // Add tag combobox and container to toolbar
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_tagComboBox);
    m_toolbar->addWidget(m_scrollArea);

    // Setup table
    m_layout->addWidget(m_table);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({"No.", "Time", "PID", "TID", "Level", "Tag", "Message"});
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch); // Message column stretches
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &LogcatViewer::handleSort);
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

bool LogcatViewer::loadContent(const QByteArray& content)
{
    m_entries.clear();
    m_table->setRowCount(0);
    m_directMatches.clear();
    m_uniqueTags.clear();
    m_tagComboBox->clear();
    
    // Clear selected tags
    while (m_tagsLayout->count() > 1) { // Keep the stretch
        QLayoutItem* item = m_tagsLayout->takeAt(0);
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_selectedTags.clear();

    QTextStream in(content);
    while (!in.atEnd()) {
        QString line = in.readLine();
        parseLogLine(line);
    }

    updateVisibleRows();
    return true;
}

void LogcatViewer::parseLogLine(const QString& line)
{
    // Example logcat format: "05-09 17:49:51.123  1234  5678 D Tag    : Message here"
    static QRegularExpression re(R"((\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+)\s+(\d+)\s+([VDIWEF])\s+([^:]+)\s*:\s*(.*))");
    
    auto match = re.match(line);
    if (match.hasMatch()) {
        LogEntry entry;
        entry.timestamp = match.captured(1);
        entry.pid = match.captured(2);
        entry.tid = match.captured(3);
        entry.level = LogEntry::parseLevel(match.captured(4)[0]);
        entry.tag = match.captured(5).trimmed();
        entry.message = match.captured(6);
        
        m_entries.append(entry);
        m_directMatches.append(false);  // Initialize as not a direct match
        
        int row = m_table->rowCount();
        m_table->insertRow(row);
        
        QColor color = LogEntry::levelColor(entry.level);
        
        // Line number
        QTableWidgetItem* numItem = new QTableWidgetItem();
        numItem->setData(Qt::DisplayRole, row + 1);
        numItem->setForeground(color);
        m_table->setItem(row, 0, numItem);
        
        // Timestamp
        QTableWidgetItem* timeItem = new QTableWidgetItem(entry.timestamp);
        timeItem->setForeground(color);
        m_table->setItem(row, 1, timeItem);
        
        // PID
        QTableWidgetItem* pidItem = new QTableWidgetItem();
        pidItem->setData(Qt::DisplayRole, entry.pid.toLongLong());
        pidItem->setForeground(color);
        m_table->setItem(row, 2, pidItem);
        
        // TID
        QTableWidgetItem* tidItem = new QTableWidgetItem();
        tidItem->setData(Qt::DisplayRole, entry.tid.toLongLong());
        tidItem->setForeground(color);
        m_table->setItem(row, 3, tidItem);
        
        // Level
        QTableWidgetItem* levelItem = new QTableWidgetItem(LogEntry::levelToString(entry.level));
        levelItem->setForeground(color);
        m_table->setItem(row, 4, levelItem);
        
        // Tag
        QTableWidgetItem* tagItem = new QTableWidgetItem(entry.tag);
        tagItem->setForeground(color);
        m_table->setItem(row, 5, tagItem);

        // Update tag collection and combobox
        if (!entry.tag.isEmpty() && !m_uniqueTags.contains(entry.tag)) {
            m_uniqueTags.insert(entry.tag);
            m_tagComboBox->addItem(entry.tag);
        }
        
        // Message
        QTableWidgetItem* msgItem = new QTableWidgetItem(entry.message);
        msgItem->setForeground(color);
        m_table->setItem(row, 6, msgItem);
    }
}

void LogcatViewer::toggleLevel(LogEntry::Level level, bool enabled)
{
    m_levelFilters[level] = enabled;
    updateVisibleRows();
}

bool LogcatViewer::matchesFilter(const LogEntry& entry) const
{
    // Check level filter
    if (!m_levelFilters[entry.level]) {
        return false;
    }
    
    // Check tag filters
    if (!m_selectedTags.isEmpty()) {
        bool matchesAnyTag = false;
        for (const QString& tag : m_selectedTags) {
            if (entry.tag.compare(tag, Qt::CaseInsensitive) == 0) {
                matchesAnyTag = true;
                break;
            }
        }
        if (!matchesAnyTag) {
            return false;
        }
    }
    
    // Check text filter
    if (m_filterQuery.isEmpty()) {
        return true;
    }
    
    return entry.message.contains(m_filterQuery, Qt::CaseInsensitive);
}

void LogcatViewer::updateVisibleRows()
{
    if (m_entries.isEmpty()) {
        return;
    }

    // First pass: identify direct matches
    m_directMatches.resize(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        m_directMatches[i] = matchesFilter(m_entries[i]);
    }

    // Second pass: show/hide rows considering context lines
    for (int i = 0; i < m_entries.size(); ++i) {
        bool shouldShow = m_directMatches[i];

        if (!shouldShow) {
            // Check if this line should be shown as context before a match
            for (int j = 1; j <= m_contextLinesBefore && i + j < m_entries.size(); ++j) {
                if (m_directMatches[i + j]) {
                    shouldShow = true;
                    break;
                }
            }
        }

        if (!shouldShow) {
            // Check if this line should be shown as context after a match
            for (int j = 1; j <= m_contextLinesAfter && i - j >= 0; ++j) {
                if (m_directMatches[i - j]) {
                    shouldShow = true;
                    break;
                }
            }
        }

        m_table->setRowHidden(i, !shouldShow);
    }
}

void LogcatViewer::applyFilter(const QString& query, int contextLinesBefore, int contextLinesAfter)
{
    m_filterQuery = query;
    m_contextLinesBefore = contextLinesBefore;
    m_contextLinesAfter = contextLinesAfter;
    updateVisibleRows();
}

void LogcatViewer::handleSort(int column, Qt::SortOrder order)
{
    m_table->sortItems(column, order);
}

void LogcatViewer::setSortRole(QTableWidgetItem* item, int column, int row) const
{
    switch (column) {
        case 0: // No.
            item->setData(Qt::UserRole, static_cast<qlonglong>(row + 1));
            break;
        case 1: { // Time
            // Convert MM-DD HH:MM:SS.mmm to QDateTime
            QString timestamp = item->text();
            QStringList parts = timestamp.split(' ');
            if (parts.size() == 2) {
                QString dateStr = parts[0]; // MM-DD
                QString timeStr = parts[1]; // HH:MM:SS.mmm
                
                // Parse date parts
                QStringList dateParts = dateStr.split('-');
                if (dateParts.size() == 2) {
                    int month = dateParts[0].toInt();
                    int day = dateParts[1].toInt();
                    
                    // Parse time parts
                    QStringList timeParts = timeStr.split(':');
                    if (timeParts.size() == 3) {
                        int hour = timeParts[0].toInt();
                        int minute = timeParts[1].toInt();
                        
                        // Handle seconds and milliseconds
                        QStringList secondParts = timeParts[2].split('.');
                        int second = secondParts[0].toInt();
                        int msec = secondParts.size() > 1 ? secondParts[1].toInt() : 0;
                        
                        // Create QDateTime (use current year since logcat doesn't include it)
                        QDateTime dt(QDate(QDate::currentDate().year(), month, day),
                                  QTime(hour, minute, second, msec));
                        item->setData(Qt::UserRole, dt);
                    }
                }
            }
            break;
        }
        case 2: { // PID
            bool ok;
            qlonglong pid = item->text().toLongLong(&ok);
            item->setData(Qt::UserRole, ok ? pid : 0);
        }
            break;
        case 3: { // TID
            bool ok;
            qlonglong tid = item->text().toLongLong(&ok);
            item->setData(Qt::UserRole, ok ? tid : 0);
            break;
        }
        case 4: { // Level
            // Sort by severity (Fatal->Error->Warning->Info->Debug->Verbose)
            static const QMap<QString, int> levelOrder = {
                {"Fatal", 0},
                {"Error", 1},
                {"Warning", 2},
                {"Info", 3},
                {"Debug", 4},
                {"Verbose", 5}
            };
            item->setData(Qt::UserRole, levelOrder.value(item->text(), 999));
            break;
        }
        case 5: // Tag
        case 6: // Message
            item->setData(Qt::UserRole, item->text().toLower()); // Case-insensitive sort
            break;
    }
}
