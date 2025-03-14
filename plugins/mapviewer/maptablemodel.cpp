#include "maptablemodel.h"
#include <QRegularExpression>

MapTableModel::MapTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    m_headers << tr("Message") << tr("Latitude") << tr("Longitude");
}

int MapTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleRows.size();
}

int MapTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 3; // Message, Latitude, Longitude
}

QVariant MapTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_visibleRows.size())
        return QVariant();

    if (role == Qt::DisplayRole) {
        const int sourceRow = m_visibleRows[index.row()];
        if (sourceRow < 0 || sourceRow >= m_entries.size())
            return QVariant();

        const QString message = m_entries[sourceRow].getMessage();
        QGeoCoordinate coord = getCoordinate(index.row());

        switch (index.column()) {
        case 0:
            return message;
        case 1:
            return coord.isValid() ? QString::number(coord.latitude(), 'f', 6) : QString();
        case 2:
            return coord.isValid() ? QString::number(coord.longitude(), 'f', 6) : QString();
        }
    }

    return QVariant();
}

QVariant MapTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        return m_headers.value(section);
    }

    return QVariant();
}

void MapTableModel::setLogs(const QList<LogEntry>& logs)
{
    beginResetModel();
    m_entries = logs;
    m_visibleRows.resize(logs.size());
    m_filteredLines.clear();

    // Initialize visible rows with entries that have valid coordinates
    int visibleCount = 0;
    for (int i = 0; i < logs.size(); ++i) {
        double lat = 0.0, lon = 0.0;
        if (tryParseCoordinates(logs[i].getMessage(), lat, lon)) {
            m_visibleRows[visibleCount++] = i;
        }
    }
    m_visibleRows.resize(visibleCount);

    endResetModel();
}

void MapTableModel::setFilter(const FilterOptions& options)
{
    beginResetModel();
    
    if (options.query.isEmpty()) {
        m_filteredLines.clear();
        // Show all entries with valid coordinates
        int visibleCount = 0;
        m_visibleRows.resize(m_entries.size());
        for (int i = 0; i < m_entries.size(); ++i) {
            double lat = 0.0, lon = 0.0;
            if (tryParseCoordinates(m_entries[i].getMessage(), lat, lon)) {
                m_visibleRows[visibleCount++] = i;
            }
        }
        m_visibleRows.resize(visibleCount);
    } else {
        QRegularExpression regex(options.inRegexMode ? options.query : 
            QRegularExpression::escape(options.query),
            options.caseSensitivity == Qt::CaseInsensitive ? 
            QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);

        m_filteredLines.clear();
        int visibleCount = 0;
        m_visibleRows.resize(m_entries.size());
        
        for (int i = 0; i < m_entries.size(); ++i) {
            const QString& message = m_entries[i].getMessage();
            bool matches = message.contains(regex);
            if (matches != options.invertFilter) {
                double lat = 0.0, lon = 0.0;
                if (tryParseCoordinates(message, lat, lon)) {
                    m_visibleRows[visibleCount++] = i;
                }
                m_filteredLines.insert(i);
            }
        }
        m_visibleRows.resize(visibleCount);
    }

    endResetModel();
}

QSet<int> MapTableModel::filteredLines() const
{
    return m_filteredLines;
}

void MapTableModel::synchronizeFilteredLines(const QSet<int>& lines)
{
    if (m_filteredLines != lines) {
        beginResetModel();
        m_filteredLines = lines;
        
        // Update visible rows to only show entries with coordinates that aren't filtered
        int visibleCount = 0;
        m_visibleRows.resize(m_entries.size());
        for (int i = 0; i < m_entries.size(); ++i) {
            if (!m_filteredLines.contains(i)) {
                double lat = 0.0, lon = 0.0;
                if (tryParseCoordinates(m_entries[i].getMessage(), lat, lon)) {
                    m_visibleRows[visibleCount++] = i;
                }
            }
        }
        m_visibleRows.resize(visibleCount);
        
        endResetModel();
    }
}

QGeoCoordinate MapTableModel::getCoordinate(int row) const
{
    if (row < 0 || row >= m_visibleRows.size())
        return QGeoCoordinate();

    const int sourceRow = m_visibleRows[row];
    if (sourceRow < 0 || sourceRow >= m_entries.size())
        return QGeoCoordinate();

    const QString message = m_entries[sourceRow].getMessage();
    double lat = 0.0, lon = 0.0;
    
    if (tryParseCoordinates(message, lat, lon)) {
        return QGeoCoordinate(lat, lon);
    }
    
    return QGeoCoordinate();
}

QString MapTableModel::getMessage(int row) const
{
    if (row < 0 || row >= m_visibleRows.size())
        return QString();

    const int sourceRow = m_visibleRows[row];
    if (sourceRow < 0 || sourceRow >= m_entries.size())
        return QString();

    return m_entries[sourceRow].getMessage();
}

bool MapTableModel::tryParseCoordinates(const QString& text, double& latitude, double& longitude) const
{
    // First try to remove any timestamp patterns to avoid false positives
    QString cleanText = text;
    
    // Common timestamp patterns
    static const QRegularExpression timestampRegex(
        R"(^\[?\d{4}[-/]\d{2}[-/]\d{2}[T\s]\d{2}:\d{2}(?::\d{2}(?:\.\d+)?)?(?:\s*[+-]\d{2}:?\d{2}|Z)?\]?\s*)",
        QRegularExpression::CaseInsensitiveOption
    );

    QRegularExpressionMatch timestampMatch = timestampRegex.match(cleanText);
    if (timestampMatch.hasMatch()) {
        cleanText.remove(0, timestampMatch.capturedLength(0));
    }

    return tryParseDecimalDegrees(cleanText, latitude, longitude) || 
           tryParseDMS(cleanText, latitude, longitude);
}

bool MapTableModel::tryParseDecimalDegrees(const QString& text, double& latitude, double& longitude) const
{
    // Try various patterns
    static const QRegularExpression simpleRegex(
        R"(([-+]?\d+\.?\d*)\s*°?\s*([NSns])?\s*[,\s]+\s*([-+]?\d+\.?\d*)\s*°?\s*([EWew])?)",
        QRegularExpression::CaseInsensitiveOption
    );

    static const QRegularExpression labeledRegex(
        R"((?:lat(?:itude)?:?\s*)?([-+]?\d+\.?\d*)\s*°?\s*([NSns])?\s*[,\s]+\s*(?:lon(?:g(?:itude)?)?:?\s*)?([-+]?\d+\.?\d*)\s*°?\s*([EWew])?)",
        QRegularExpression::CaseInsensitiveOption
    );

    QRegularExpressionMatch match = simpleRegex.match(text);
    if (!match.hasMatch()) {
        match = labeledRegex.match(text);
        if (!match.hasMatch()) {
            return false;
        }
    }

    bool ok1 = false, ok2 = false;
    double lat = match.captured(1).toDouble(&ok1);
    double lon = match.captured(3).toDouble(&ok2);

    if (!ok1 || !ok2)
        return false;

    // Handle cardinal directions
    QString ns = match.captured(2).toUpper();
    QString ew = match.captured(4).toUpper();

    if (ns == "S")
        lat = -lat;
    if (ew == "W")
        lon = -lon;

    // Check if coordinates are within valid ranges
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180)
        return false;

    latitude = lat;
    longitude = lon;
    return true;
}

bool MapTableModel::tryParseDMS(const QString& text, double& latitude, double& longitude) const
{
    // Match patterns like:
    // 40°42'51.0"N 74°00'21.0"W
    // 40° 42' 51"N, 74° 0' 21"W
    static const QRegularExpression regex(
        R"((\d+)\s*°\s*(\d+)\s*'\s*(\d+(?:\.\d+)?)\s*"?\s*([NSns])\s*[,\s]+\s*(\d+)\s*°\s*(\d+)\s*'\s*(\d+(?:\.\d+)?)\s*"?\s*([EWew]))",
        QRegularExpression::CaseInsensitiveOption
    );

    QRegularExpressionMatch match = regex.match(text);
    if (!match.hasMatch())
        return false;

    bool ok = false;
    double latDeg = match.captured(1).toDouble(&ok);
    if (!ok || latDeg > 90) return false;
    
    double latMin = match.captured(2).toDouble(&ok);
    if (!ok || latMin >= 60) return false;
    
    double latSec = match.captured(3).toDouble(&ok);
    if (!ok || latSec >= 60) return false;

    double lonDeg = match.captured(5).toDouble(&ok);
    if (!ok || lonDeg > 180) return false;
    
    double lonMin = match.captured(6).toDouble(&ok);
    if (!ok || lonMin >= 60) return false;
    
    double lonSec = match.captured(7).toDouble(&ok);
    if (!ok || lonSec >= 60) return false;

    // Convert to decimal degrees
    latitude = latDeg + (latMin / 60.0) + (latSec / 3600.0);
    longitude = lonDeg + (lonMin / 60.0) + (lonSec / 3600.0);

    // Apply cardinal directions
    if (match.captured(4).toUpper() == "S")
        latitude = -latitude;
    if (match.captured(8).toUpper() == "W")
        longitude = -longitude;

    // Final range check after applying direction
    if (latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180)
        return false;

    return true;
}
