#include "plaintextviewer.h"
#include <QHeaderView>
#include <QSet>
#include <algorithm>

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
        case 0:
            return m_visibleRows[index.row()] + 1; // Line number (1-based)
        case 1:
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
        case 0:
            return tr("No.");
        case 1:
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
        for (int i = 0; i < m_entries.size(); ++i) {
            m_visibleRows[i] = i;
        }
    } else {
        // Find matches
        QVector<int> matchIndices;
        for (int i = 0; i < m_entries.size(); ++i) {
            QString line = m_entries[i].getMessage();
            if (line.contains(options.query, options.caseSensitivity)) {
                matchIndices.append(i);
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

PlainTextViewer::PlainTextViewer()
    : m_tableView(new QTableView())
    , m_model(new PlainTextTableModel(this))
{
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableView->verticalHeader()->hide();
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
}

PlainTextViewer::~PlainTextViewer()
{
    delete m_tableView;
}

bool PlainTextViewer::loadContent(const QVector<LogEntry>& content)
{
    m_model->setLogEntries(content);
    return true;
}

void PlainTextViewer::applyFilter(const FilterOptions& options)
{
    m_model->applyFilter(options);
}
