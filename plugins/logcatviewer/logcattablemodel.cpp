#include "logcattablemodel.h"

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

    int sourceRow = mapToSourceRow(index.row());
    if (sourceRow >= m_entries.size())
        return QVariant();

    const LogcatEntry& entry(m_entries[sourceRow]);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case LogcatColumn::No:
            return sourceRow + 1; // Line number (1-based)
        case LogcatColumn::Time:
            return entry.timestamp;
        case LogcatColumn::Pid:
            return entry.pid;
        case LogcatColumn::Tid:
            return entry.tid;
        case LogcatColumn::Level:
            return LogcatEntry::levelToString(entry.level);
        case LogcatColumn::Tag:
            return entry.tag;
        case LogcatColumn::Message:
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
        case LogcatColumn::No:
        case LogcatColumn::Pid:
        case LogcatColumn::Tid:
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
    case LogcatColumn::No: return tr("No.");
    case LogcatColumn::Time: return tr("Time");
    case LogcatColumn::Pid: return tr("PID");
    case LogcatColumn::Tid: return tr("TID");
    case LogcatColumn::Level: return tr("Level");
    case LogcatColumn::Tag: return tr("Tag");
    case LogcatColumn::Message: return tr("Message");
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
            const LogcatEntry& leftEntry(m_entries[left]);
            const LogcatEntry& rightEntry(m_entries[right]);
            
            bool lessThan;
            switch (column) {
            case LogcatColumn::No: // Line number
                lessThan = left < right;
                break;
            case LogcatColumn::Time:
                lessThan = leftEntry.timestamp < rightEntry.timestamp;
                break;
            case LogcatColumn::Pid:
                {
                    bool leftOk = false, rightOk = false;
                    qint64 leftPid = 0, rightPid = 0;
                    
                    if (!leftEntry.pid.isEmpty()) {
                        leftPid = leftEntry.pid.toLongLong(&leftOk);
                    }
                    if (!rightEntry.pid.isEmpty()) {
                        rightPid = rightEntry.pid.toLongLong(&rightOk);
                    }
                    
                    if (leftOk == rightOk) {
                        lessThan = leftOk ? leftPid < rightPid : leftEntry.pid < rightEntry.pid;
                    } else {
                        lessThan = leftOk < rightOk; // Invalid values sort before valid ones
                    }
                }
                break;
            case LogcatColumn::Tid:
                {
                    bool leftOk = false, rightOk = false;
                    qint64 leftTid = 0, rightTid = 0;
                    
                    if (!leftEntry.tid.isEmpty()) {
                        leftTid = leftEntry.tid.toLongLong(&leftOk);
                    }
                    if (!rightEntry.tid.isEmpty()) {
                        rightTid = rightEntry.tid.toLongLong(&rightOk);
                    }
                    
                    if (leftOk == rightOk) {
                        lessThan = leftOk ? leftTid < rightTid : leftEntry.tid < rightEntry.tid;
                    } else {
                        lessThan = leftOk < rightOk; // Invalid values sort before valid ones
                    }
                }
                break;
            case LogcatColumn::Level:
                lessThan = leftEntry.level < rightEntry.level;
                break;
            case LogcatColumn::Tag:
                lessThan = leftEntry.tag < rightEntry.tag;
                break;
            case LogcatColumn::Message:
                lessThan = leftEntry.message < rightEntry.message;
                break;
            default:
                lessThan = false;
            }
            return lessThan;
        });
    
    // Apply sort order
    if (order == Qt::DescendingOrder) {
        std::reverse(m_visibleRows.begin(), m_visibleRows.end());
    }
    
    endResetModel();
}

void LogcatTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    m_visibleRows.resize(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        m_visibleRows[i] = i;
    }
    endResetModel();
}

void LogcatTableModel::setVisibleRows(const QList<int>& linesToShow)
{
    beginResetModel();
    m_visibleRows = linesToShow;
    sort(m_sortColumn, m_sortOrder);
    endResetModel();
}
