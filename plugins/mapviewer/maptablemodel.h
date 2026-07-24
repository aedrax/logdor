#ifndef MAPTABLEMODEL_H
#define MAPTABLEMODEL_H

#include <logdor/FileSource.h>
#include <logdor/GeoScan.h>
#include <logdor/LineIndex.h>

#include <QAbstractTableModel>

#include <memory>

/**
 * The location-event list: one row per coordinate hit currently visible
 * (post-filter). Small by construction - coordinate hits are sparse and the
 * map caps what it plots - so messages are read on demand with no cache.
 */
class MapTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Line = 0, Latitude, Longitude, Message, ColumnCount };

    explicit MapTableModel(QObject* parent = nullptr);

    void setSource(std::shared_ptr<logdor::FileSource> source,
                   std::shared_ptr<const logdor::LineIndex> index);
    void setPoints(QList<logdor::GeoPoint> points);
    const QList<logdor::GeoPoint>& points() const { return m_points; }

    /// Row for a source line, -1 when absent (points are line-ascending).
    int rowForLine(qint32 line) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    QList<logdor::GeoPoint> m_points;
};

#endif // MAPTABLEMODEL_H
