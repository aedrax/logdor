#ifndef MAPTABLEMODEL_H
#define MAPTABLEMODEL_H

#include <QtCore/QAbstractTableModel>
#include <QtCore/QList>
#include <QtCore/QStringList>
#include <QtPositioning/QGeoCoordinate>
#include "../../app/src/plugininterface.h"

class MapTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit MapTableModel(QObject* parent = nullptr);

    // Required QAbstractTableModel overrides
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Custom methods
    void setLogs(const QList<LogEntry>& logs);
    void setFilter(const FilterOptions& options);
    QSet<int> filteredLines() const;
    void synchronizeFilteredLines(const QSet<int>& lines);
    
    // Get the original index for a visible row
    int mapToSourceRow(int row) const {
        if (row < 0 || row >= m_visibleRows.size())
            return -1;
        
        const int sourceRow = m_visibleRows[row];
        if (sourceRow < 0 || sourceRow >= m_entries.size())
            return -1;
            
        return sourceRow;
    }
    
    // Get coordinate for a visible row
    QGeoCoordinate getCoordinate(int row) const;
    
    // Get message for a visible row
    QString getMessage(int row) const;

private:
    QList<LogEntry> m_entries;        // Original log entries
    QVector<int> m_visibleRows;       // Indices of visible rows
    QStringList m_headers;
    QSet<int> m_filteredLines;

    // Coordinate parsing helpers
    bool tryParseCoordinates(const QString& text, double& latitude, double& longitude) const;
    bool tryParseDecimalDegrees(const QString& text, double& latitude, double& longitude) const;
    bool tryParseDMS(const QString& text, double& latitude, double& longitude) const;
};

#endif // MAPTABLEMODEL_H
