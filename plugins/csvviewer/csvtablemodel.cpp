#include "csvtablemodel.h"
#include <QtConcurrent/QtConcurrent>

CsvTableModel::CsvTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int CsvTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    // Subtract 1 to account for header row
    return m_visibleRows.size() > 0 ? m_visibleRows.size() - 1 : 0;
}

int CsvTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_headers.size();
}

QVariant CsvTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_visibleRows.size() - 1)
        return QVariant();

    // Add 1 to row index to skip header row
    const CsvEntry& entry(m_entries[m_visibleRows[index.row() + 1]]);

    if (role == Qt::DisplayRole) {
        return entry.getValue(index.column());
    }
    else if (role == Qt::TextAlignmentRole) {
        // Try to detect if the column contains numbers
        bool ok;
        entry.getValue(index.column()).toDouble(&ok);
        if (ok) {
            return int(Qt::AlignRight | Qt::AlignVCenter);
        }
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }

    return QVariant();
}

QVariant CsvTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal && section < m_headers.size()) {
        return m_headers[section];
    }
    else if (orientation == Qt::Vertical) {
        // Add 1 to make row numbers 1-based
        return section + 1;
    }

    return QVariant();
}

void CsvTableModel::sort(int column, Qt::SortOrder order)
{
    if (m_visibleRows.size() <= 1) // Nothing to sort if only header row
        return;

    beginResetModel();
    
    m_sortColumn = column;
    m_sortOrder = order;
    
    // Start from index 1 to preserve header row at index 0
    auto begin = m_visibleRows.begin() + 1;
    auto end = m_visibleRows.end();
    
    std::sort(begin, end, 
        [this, column, order](int left, int right) {
            const CsvEntry& leftEntry(m_entries[left]);
            const CsvEntry& rightEntry(m_entries[right]);
            
            QVariant leftValue = leftEntry.getValue(column);
            QVariant rightValue = rightEntry.getValue(column);
            
            // Try to compare as numbers if possible
            bool leftOk, rightOk;
            double leftNum = leftValue.toDouble(&leftOk);
            double rightNum = rightValue.toDouble(&rightOk);
            
            bool lessThan;
            if (leftOk && rightOk) {
                lessThan = leftNum < rightNum;
            } else {
                lessThan = leftValue.toString() < rightValue.toString();
            }
            
            return order == Qt::AscendingOrder ? lessThan : !lessThan;
        });
    
    endResetModel();
}

void CsvTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    
    m_entries.clear();
    m_visibleRows.clear();
    m_headers.clear();
    
    // Convert all entries to CsvEntries
    for (const LogEntry& entry : entries) {
        m_entries.append(CsvEntry(entry));
    }
    
    if (!m_entries.isEmpty()) {
        // First row contains headers
        m_headers = m_entries[0].values;
        
        // Setup visible rows (all rows initially visible)
        m_visibleRows.resize(m_entries.size());
        for (int i = 0; i < m_entries.size(); ++i) {
            m_visibleRows[i] = i;
        }
    }
    
    endResetModel();
}

void CsvTableModel::setVisibleRows(const QList<int>& linesToShow)
{
    beginResetModel();
    m_visibleRows = linesToShow;
    // Ensure header row (index 0) is always included
    if (!m_visibleRows.isEmpty() && !m_visibleRows.contains(0)) {
        m_visibleRows.prepend(0);
    }
    sort(m_sortColumn, m_sortOrder);
    endResetModel();
}
