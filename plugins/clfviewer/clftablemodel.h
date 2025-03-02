#ifndef CLFTABLEMODEL_H
#define CLFTABLEMODEL_H

#include "clfentry.h"
#include <QAbstractTableModel>
#include <QList>

enum CLFColumn {
    No,
    RemoteHost,
    Identity,
    UserId,
    Timestamp,
    Method,
    Path,
    Protocol,
    Status,
    BytesSent
};

class CLFTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit CLFTableModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    
    void setLogEntries(const QList<LogEntry>& entries);
    void setFilter(const FilterOptions& options);
    int mapToSourceRow(int visibleRow) const { return m_visibleRows[visibleRow]; }
    int mapFromSourceRow(int sourceRow) const { return m_visibleRows.indexOf(sourceRow); }

private:
    QList<CLFEntry> m_entries;
    QList<int> m_visibleRows;  // Indices into m_entries for filtered view
    
    bool matchesFilter(const CLFEntry& entry, const FilterOptions& options) const;
    void applyFilter(const FilterOptions& options);
};

#endif // CLFTABLEMODEL_H
