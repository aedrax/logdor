#pragma once

#include "logdor/FileSource.h"
#include "logdor/FilterScan.h" // kDefaultFilterChunkLines
#include "logdor/LineIndex.h"

#include <QFuture>

#include <memory>
#include <vector>

namespace logdor {

/// A geographic coordinate found on a log line. Plain doubles - core stays
/// free of QtPositioning.
struct GeoPoint {
    qint32 line = -1;
    double latitude = 0.0;
    double longitude = 0.0;
};

struct GeoScanResult {
    std::vector<GeoPoint> points; // ascending by line
    qint64 elapsedMs = 0;
};

/**
 * Extract a coordinate pair from one line, if present. Ported from the
 * legacy Map Viewer: strips a leading timestamp, then tries decimal-degree
 * patterns ("40.71, -74.00", "lat=40.71 lon=-74.00", "latitude: 40.71°...
 * longitude: ...") and DMS ("40°42'51\"N 74°00'21\"W"), with hemisphere
 * suffixes and ±90/±180 range validation.
 */
bool parseCoordinates(QByteArrayView raw, double* latitude, double* longitude);

/**
 * Find every line carrying a coordinate pair. Chunk-parallel on worker
 * threads; same QPromise contract as scanFilter (cancellable between
 * super-chunks, permille progress).
 */
QFuture<GeoScanResult> scanCoordinates(
    std::shared_ptr<FileSource> source,
    std::shared_ptr<const LineIndex> index,
    qint64 linesPerChunk = kDefaultFilterChunkLines);

} // namespace logdor
