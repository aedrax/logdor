#ifndef LOGCATTABLEMODEL_H
#define LOGCATTABLEMODEL_H

#include <QAbstractTableModel>
#include <QSet>
#include <QVector>
#include <QMap>

#include "../../app/src/plugininterface.h"
#include "logcatentry.h"

class LogcatTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit LogcatTableModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    
    void setLogEntries(const QVector<LogEntry>& entries);
    void applyFilter(const FilterOptions& filterOptions, const QSet<QString>& tags, 
                    const QMap<LogcatEntry::Level, bool>& levelFilters);
    QSet<QString> getUniqueTags() const;
    LogcatEntry logEntryToLogcatEntry(const LogEntry& entry) const;
    int mapToSourceRow(int visibleRow) const { return m_visibleRows[visibleRow]; }

private:
    bool matchesFilter(const LogcatEntry& entry, const QString& query, Qt::CaseSensitivity caseSensitivity,
                      const QSet<QString>& tags,
                      const QMap<LogcatEntry::Level, bool>& levelFilters) const;

    QVector<LogEntry> m_entries;
    QVector<int> m_visibleRows;  // Indices into m_entries for filtered view
    int m_sortColumn{0};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
};

#endif // LOGCATTABLEMODEL_H