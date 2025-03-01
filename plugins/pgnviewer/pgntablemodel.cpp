#include "pgntablemodel.h"
#include <QRegularExpression>
#include <algorithm>

PgnTableModel::PgnTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int PgnTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleRows.size();
}

int PgnTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 3; // Move number, White move, Black move
}

QVariant PgnTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (role == Qt::DisplayRole) {
        const ChessMove& move = m_moves[m_visibleRows[index.row()]];
        switch (index.column()) {
            case PgnColumn::MoveNumber:
                return move.number;
            case PgnColumn::WhiteMove:
                return move.white;
            case PgnColumn::BlackMove:
                return move.black;
        }
    }

    return QVariant();
}

QVariant PgnTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        switch (section) {
            case PgnColumn::MoveNumber:
                return tr("#");
            case PgnColumn::WhiteMove:
                return tr("White");
            case PgnColumn::BlackMove:
                return tr("Black");
        }
    }

    return QVariant();
}

void PgnTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    m_moves.clear();
    
    // Combine all log entries into one string for parsing
    QString fullContent;
    for (const auto& entry : entries) {
        fullContent += entry.getMessage() + "\n";
    }
    
    parsePgnMoves(fullContent);
    
    // Initialize visible rows to show all moves
    m_visibleRows.resize(m_moves.size());
    std::iota(m_visibleRows.begin(), m_visibleRows.end(), 0);
    
    endResetModel();
}

void PgnTableModel::setFilter(const FilterOptions& options)
{
    beginResetModel();
    
    m_visibleRows.clear();
    const QString& query = options.query.toLower();
    
    // If no filter, show all moves
    if (query.isEmpty()) {
        m_visibleRows.resize(m_moves.size());
        std::iota(m_visibleRows.begin(), m_visibleRows.end(), 0);
    } else {
        // Filter moves based on the query
        for (int i = 0; i < m_moves.size(); ++i) {
            const auto& move = m_moves[i];
            bool matches = move.white.toLower().contains(query) || 
                         move.black.toLower().contains(query);
            
            // Add the move if:
            // - Not inverted and it matches, OR
            // - Inverted and it doesn't match
            if (matches != options.invertFilter) {
                m_visibleRows.append(i);
            }
        }
    }
    
    endResetModel();
}

void PgnTableModel::parsePgnMoves(const QString& content)
{
    // Remove PGN tags and comments
    QString cleanContent = content;
    cleanContent.remove(QRegularExpression("\\[.*?\\]"));
    cleanContent.remove(QRegularExpression("\\{.*?\\}"));
    
    // Split into moves
    QRegularExpression moveRegex("(\\d+)\\.\\s*([\\w\\+\\-\\#\\=\\!\\?]+)(?:\\s+([\\w\\+\\-\\#\\=\\!\\?]+))?");
    QRegularExpressionMatchIterator matches = moveRegex.globalMatch(cleanContent);
    
    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        ChessMove move;
        move.number = match.captured(1).toInt();
        move.white = match.captured(2);
        move.black = match.captured(3);
        m_moves.append(move);
    }
}
