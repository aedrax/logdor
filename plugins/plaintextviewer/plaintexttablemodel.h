#ifndef PLAINTEXTTABLEMODEL_H
#define PLAINTEXTTABLEMODEL_H

#include "../../app/src/plugininterface.h"
#include <QAbstractTableModel>
#include <QTableView>
#include <QString>
#include <QtPlugin>
#include <QItemSelection>

enum PlainTextColumn {
    No,
    Log
};

class PlainTextTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit PlainTextTableModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    
    void setLogEntries(const QList<LogEntry>& entries);
    void setFilter(const FilterOptions& options);
    int mapToSourceRow(int visibleRow) const { return m_visibleRows[visibleRow]; }

private:
    QList<LogEntry> m_entries;
    QList<int> m_visibleRows;  // Indices into m_entries for filtered view
};

#endif // PLAINTEXTTABLEMODEL_H