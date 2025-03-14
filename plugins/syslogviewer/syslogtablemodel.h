#ifndef SYSLOGTABLEMODEL_H
#define SYSLOGTABLEMODEL_H

#include <QtCore/QAbstractTableModel>
#include <QtCore/QSet>
#include <QtCore/QList>
#include <QtCore/QMap>

#include "../../app/src/plugininterface.h"
#include "syslogentry.h"

// No., Time, Hostname, Process[PID], Facility, Severity, Message
enum SyslogColumn {
    No,
    Time,
    Hostname,
    Process,
    Facility,
    Severity,
    Message
};

class SyslogTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit SyslogTableModel(QObject* parent = nullptr);
    
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
    bool matchesFilter(const SyslogEntry& entry, const QString& query, Qt::CaseSensitivity caseSensitivity,
                      const QSet<QString>& facilities,
                      const QMap<SyslogEntry::Severity, bool>& severityFilters) const;

    QList<LogEntry> m_entries;
    QList<int> m_visibleRows;  // Indices into m_entries for filtered view
    int m_sortColumn{0};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
};

#endif // SYSLOGTABLEMODEL_H
