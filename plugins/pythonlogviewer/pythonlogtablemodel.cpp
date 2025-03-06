#include <QtConcurrent/QtConcurrent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "pythonlogtablemodel.h"

PythonLogTableModel::PythonLogTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int PythonLogTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleRows.size();
}

int PythonLogTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 4; // No., Level, Module, Message
}

QVariant PythonLogTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_visibleRows.size())
        return QVariant();

    const PythonLogEntry entry(m_entries[m_visibleRows[index.row()]]);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case PythonLogColumn::No:
            return m_visibleRows[index.row()] + 1; // Line number (1-based)
        case PythonLogColumn::Level:
            return PythonLogEntry::levelToString(entry.level);
        case PythonLogColumn::Module:
            return entry.module;
        case PythonLogColumn::Message:
            return entry.message;
        }
    }
    else if (role == Qt::BackgroundRole) {
        return PythonLogEntry::levelColor(entry.level);
    }
    else if (role == Qt::ForegroundRole) {
        return QColor(Qt::black);
    }
    else if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case PythonLogColumn::No:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

QVariant PythonLogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case PythonLogColumn::No: return tr("No.");
    case PythonLogColumn::Level: return tr("Level");
    case PythonLogColumn::Module: return tr("Module");
    case PythonLogColumn::Message: return tr("Message");
    default: return QVariant();
    }
}

void PythonLogTableModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    
    m_sortColumn = column;
    m_sortOrder = order;
    
    std::sort(m_visibleRows.begin(), m_visibleRows.end(), 
        [this, column, order](int left, int right) {
            const PythonLogEntry leftEntry(m_entries[left]);
            const PythonLogEntry rightEntry(m_entries[right]);
            
            bool lessThan;
            switch (column) {
            case 0: // Line number
                lessThan = left < right;
                break;
            case 1: // Level
                lessThan = leftEntry.level < rightEntry.level;
                break;
            case 2: // Module
                lessThan = leftEntry.module < rightEntry.module;
                break;
            case 3: // Message
                lessThan = leftEntry.message < rightEntry.message;
                break;
            default:
                lessThan = false;
            }
            return order == Qt::AscendingOrder ? lessThan : !lessThan;
        });
    
    endResetModel();
}

void PythonLogTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    m_visibleRows.resize(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        m_visibleRows[i] = i;
    }
    endResetModel();
}

void PythonLogTableModel::setVisibleRows(const QList<int>& linesToShow)
{
    beginResetModel();
    m_visibleRows = linesToShow;
    sort(m_sortColumn, m_sortOrder);
    endResetModel();
}
