#include "maptablemodel.h"

#include <algorithm>

MapTableModel::MapTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void MapTableModel::setSource(std::shared_ptr<logdor::FileSource> source,
                              std::shared_ptr<const logdor::LineIndex> index)
{
    beginResetModel();
    m_source = std::move(source);
    m_index = std::move(index);
    m_points.clear();
    endResetModel();
}

void MapTableModel::setPoints(QList<logdor::GeoPoint> points)
{
    beginResetModel();
    m_points = std::move(points);
    endResetModel();
}

int MapTableModel::rowForLine(qint32 line) const
{
    const auto it = std::lower_bound(
        m_points.begin(), m_points.end(), line,
        [](const logdor::GeoPoint& p, qint32 l) { return p.line < l; });
    if (it == m_points.end() || it->line != line)
        return -1;
    return int(it - m_points.begin());
}

int MapTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_points.size());
}

int MapTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant MapTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_points.size())
        return {};
    const logdor::GeoPoint& point = m_points[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Line:
            return point.line + 1;
        case Latitude:
            return QString::number(point.latitude, 'f', 6);
        case Longitude:
            return QString::number(point.longitude, 'f', 6);
        case Message:
            if (m_source && m_index)
                return QString::fromUtf8(
                    m_source->read(m_index->offsetOf(point.line),
                                   qMin<qsizetype>(m_index->lengthOf(point.line),
                                                   500)));
            return {};
        }
    }
    if (role == Qt::TextAlignmentRole && index.column() != Message)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QVariant MapTableModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case Line: return tr("Line");
    case Latitude: return tr("Latitude");
    case Longitude: return tr("Longitude");
    case Message: return tr("Message");
    }
    return {};
}
