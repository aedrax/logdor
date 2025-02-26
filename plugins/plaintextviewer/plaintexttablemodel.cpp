#include "plaintexttablemodel.h"
#include <QtConcurrent/QtConcurrent>

PlainTextTableModel::PlainTextTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int PlainTextTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleRows.size();
}

int PlainTextTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 2; // Line number and content
}

QVariant PlainTextTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_visibleRows.size())
        return QVariant();

    const LogEntry& entry = m_entries[m_visibleRows[index.row()]];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case PlainTextColumn::No:
            return m_visibleRows[index.row()] + 1; // Line number (1-based)
        case PlainTextColumn::Log:
            return entry.getMessage();
        }
    }
    else if (role == Qt::FontRole && index.column() == 1) {
        return QFont("Monospace");
    }

    return QVariant();
}

QVariant PlainTextTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        switch (section) {
        case PlainTextColumn::No:
            return tr("No.");
        case PlainTextColumn::Log:
            return tr("Log");
        }
    }

    return QVariant();
}

void PlainTextTableModel::setLogEntries(const QVector<LogEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    m_visibleRows.resize(entries.size());
    // Initialize with all rows visible
    for (int i = 0; i < entries.size(); ++i) {
        m_visibleRows[i] = i;
    }
    endResetModel();
}

void PlainTextTableModel::applyFilter(const FilterOptions& options)
{
    beginResetModel();
    m_visibleRows.clear();

    if (options.query.isEmpty()) {
        // Show all entries
        m_visibleRows.resize(m_entries.size());
        std::iota(m_visibleRows.begin(), m_visibleRows.end(), 0);
    } else {
        // Find matches in parallel using QtConcurrent::mapped
        QVector<int> indices(m_entries.size());
        std::iota(indices.begin(), indices.end(), 0);
        
        auto future = QtConcurrent::mapped(indices, [this, &options](int i) {
            return QPair<int, bool>(i, m_entries[i].getMessage().contains(options.query, options.caseSensitivity));
        });
        
        auto results = future.results();
        
        // Collect matching indices
        QVector<int> matchIndices;
        for (const auto& result : results) {
            if (result.second) {
                matchIndices.append(result.first);
            }
        }

        // Add matches and context lines
        QSet<int> linesToShow;
        for (int matchIndex : matchIndices) {
            // Add context lines before
            for (int i = std::max(0, matchIndex - options.contextLinesBefore); i < matchIndex; ++i) {
                linesToShow.insert(i);
            }

            // Add the matching line
            linesToShow.insert(matchIndex);

            // Add context lines after
            for (int i = matchIndex + 1; i <= std::min<int>(m_entries.size() - 1, matchIndex + options.contextLinesAfter); ++i) {
                linesToShow.insert(i);
            }
        }

        // Convert to sorted vector
        m_visibleRows = linesToShow.values().toVector();
        std::sort(m_visibleRows.begin(), m_visibleRows.end());
    }

    endResetModel();
}