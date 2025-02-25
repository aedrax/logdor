#include <QtConcurrent/QtConcurrent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "logcattablemodel.h"
#include "taglabel.h"

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
            LogcatEntry logcatEntry = logEntryToLogcatEntry(entry);
            return logcatEntry.tag;
        },
        // Reduce function to combine results
        reduceTags,
        // Use parallel reduction
        QtConcurrent::ReduceOption::UnorderedReduce
    );
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
    logcatEntry.level = LogcatEntry::Level::Unknown;
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