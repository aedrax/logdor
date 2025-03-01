#ifndef PGNTABLEMODEL_H
#define PGNTABLEMODEL_H

#include "../../app/src/plugininterface.h"
#include <QAbstractTableModel>
#include <QTableView>
#include <QString>
#include <QtPlugin>
#include <QItemSelection>

enum PgnColumn {
    MoveNumber,
    WhiteMove,
    BlackMove
};

struct ChessMove {
    int number;
    QString white;
    QString black;
};

class PgnTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit PgnTableModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    
    void setLogEntries(const QList<LogEntry>& entries);
    void setFilter(const FilterOptions& options);
    int mapToSourceRow(int visibleRow) const { return m_visibleRows[visibleRow]; }
    int mapFromSourceRow(int sourceRow) const { return m_visibleRows.indexOf(sourceRow); }

private:
    QList<ChessMove> m_moves;
    QList<int> m_visibleRows;  // Indices into m_moves for filtered view
    
    // Helper method to parse PGN moves from text
    void parsePgnMoves(const QString& content);
};

#endif // PGNTABLEMODEL_H
