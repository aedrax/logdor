#ifndef PYTHONLOGTABLEMODEL_H
#define PYTHONLOGTABLEMODEL_H

#include <QAbstractTableModel>
#include <QSet>
#include <QList>
#include <QMap>

#include "../../app/src/plugininterface.h"
#include "pythonlogentry.h"

// No., Level, Module, Message
enum PythonLogColumn {
    No,
    Level,
    Module,
    Message
};

class PythonLogTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit PythonLogTableModel(QObject* parent = nullptr);
    
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
    bool matchesFilter(const PythonLogEntry& entry, const QString& query, Qt::CaseSensitivity caseSensitivity,
                      const QSet<QString>& modules,
                      const QMap<PythonLogEntry::Level, bool>& levelFilters) const;

    QList<LogEntry> m_entries;
    QList<int> m_visibleRows;  // Indices into m_entries for filtered view
    int m_sortColumn{0};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
};

#endif // PYTHONLOGTABLEMODEL_H
