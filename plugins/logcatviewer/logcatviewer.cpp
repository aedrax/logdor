#include "logcatviewer.h"
#include <QHeaderView>
#include <QTextStream>
#include <QRegularExpression>
#include <QIcon>

LogcatViewer::LogcatViewer()
    : QObject()
    , m_container(new QWidget())
    , m_layout(new QVBoxLayout(m_container))
    , m_toolbar(new QToolBar())
    , m_table(new QTableWidget())
{
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

    // Setup table
    m_layout->addWidget(m_table);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"No.", "Time", "PID", "Level", "Tag", "Message"});
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch); // Message column stretches
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &LogcatViewer::handleSort);
}

bool LogcatViewer::loadContent(const QByteArray& content)
{
    m_entries.clear();
    m_table->setRowCount(0);

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
    static QRegularExpression re(R"((\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+)\s+\d+\s+([VDIWEF])\s+([^:]+)\s*:\s*(.*))");
    
    auto match = re.match(line);
    if (match.hasMatch()) {
        LogEntry entry;
        entry.timestamp = match.captured(1);
        entry.pid = match.captured(2);
        entry.level = LogEntry::parseLevel(match.captured(3)[0]);
        entry.tag = match.captured(4).trimmed();
        entry.message = match.captured(5);
        
        m_entries.append(entry);
        
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
        
        // Level
        QTableWidgetItem* levelItem = new QTableWidgetItem(LogEntry::levelToString(entry.level));
        levelItem->setForeground(color);
        m_table->setItem(row, 3, levelItem);
        
        // Tag
        QTableWidgetItem* tagItem = new QTableWidgetItem(entry.tag);
        tagItem->setForeground(color);
        m_table->setItem(row, 4, tagItem);
        
        // Message
        QTableWidgetItem* msgItem = new QTableWidgetItem(entry.message);
        msgItem->setForeground(color);
        m_table->setItem(row, 5, msgItem);
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
    
    // Check text filter
    if (m_filterQuery.isEmpty()) {
        return true;
    }
    
    return entry.tag.contains(m_filterQuery, Qt::CaseInsensitive) ||
           entry.message.contains(m_filterQuery, Qt::CaseInsensitive);
}

void LogcatViewer::updateVisibleRows()
{
    for (int i = 0; i < m_entries.size(); ++i) {
        m_table->setRowHidden(i, !matchesFilter(m_entries[i]));
    }
}

void LogcatViewer::applyFilter(const QString& query)
{
    m_filterQuery = query;
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
        case 3: { // Level
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
        case 4: // Tag
        case 5: // Message
            item->setData(Qt::UserRole, item->text().toLower()); // Case-insensitive sort
            break;
    }
}
