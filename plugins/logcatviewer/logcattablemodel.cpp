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

    const LogcatEntry& entry(m_entries[m_visibleRows[index.row()]]);

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
            const LogcatEntry& leftEntry(m_entries[left]);
            const LogcatEntry& rightEntry(m_entries[right]);
            
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

void LogcatTableModel::applyFilter(const QVector<int>& linesToShow)
{
    beginResetModel();
    m_visibleRows.clear();
    m_visibleRows = linesToShow;
    std::sort(m_visibleRows.begin(), m_visibleRows.end());
    
    if (m_sortColumn >= 0) {
        sort(m_sortColumn, m_sortOrder);
    }
    
    endResetModel();
}
