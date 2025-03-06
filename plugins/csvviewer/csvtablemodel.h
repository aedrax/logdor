#ifndef CSVTABLEMODEL_H
#define CSVTABLEMODEL_H

#include <QAbstractItemModel>
#include <QList>
#include <QStringList>

#include "csventry.h"

class CsvTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit CsvTableModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    
    void setLogEntries(const QList<LogEntry>& entries);
    void setVisibleRows(const QList<int>& linesToShow);
    int mapToSourceRow(int visibleRow) const { return m_visibleRows[visibleRow]; }
    int mapFromSourceRow(int sourceRow) const { return m_visibleRows.indexOf(sourceRow); }

private:
    QList<CsvEntry> m_entries;
    QList<int> m_visibleRows;  // Indices into m_entries for filtered view
    QStringList m_headers;     // Column names from first row
    int m_sortColumn{0};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
};

#endif // CSVTABLEMODEL_H
