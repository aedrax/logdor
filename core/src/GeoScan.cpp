#include "logdor/GeoScan.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QThread>
#include <QtConcurrentMap>
#include <QtConcurrentRun>

#include <algorithm>

namespace logdor {

namespace {

// Legacy Map Viewer patterns, verbatim. Immutable after init; match() is
// thread-safe on shared const instances.
const QRegularExpression reTimestamp(
    R"(^\[?\d{4}[-/]\d{2}[-/]\d{2}[T\s]\d{2}:\d{2}(?::\d{2}(?:\.\d+)?)?(?:\s*[+-]\d{2}:?\d{2}|Z)?\]?\s*)",
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression reSimple(
    R"(([-+]?\d+\.?\d*)\s*°?\s*([NSns])?\s*[,\s]+\s*([-+]?\d+\.?\d*)\s*°?\s*([EWew])?)",
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression reLabeled(
    R"((?:lat(?:itude)?[=:]\s*)?([-+]?\d+\.?\d*)\s*°?\s*([NSns])?\s*[,\s]+\s*(?:lon(?:g(?:itude)?)?[=:]\s*)?([-+]?\d+\.?\d*)\s*°?\s*([EWew])?)",
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression reKeyValue(
    R"(latitude[=:]\s*([-+]?\d+\.?\d*)\s*°.*longitude[=:]\s*([-+]?\d+\.?\d*)\s*°)",
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression reDms(
    R"((\d+)\s*°\s*(\d+)\s*'\s*(\d+(?:\.\d+)?)\s*"?\s*([NSns])\s*[,\s]+\s*(\d+)\s*°\s*(\d+)\s*'\s*(\d+(?:\.\d+)?)\s*"?\s*([EWew]))",
    QRegularExpression::CaseInsensitiveOption);

bool inRange(double lat, double lon)
{
    return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

bool parseDecimalDegrees(const QString& text, double* latitude, double* longitude)
{
    if (const auto kv = reKeyValue.match(text); kv.hasMatch()) {
        bool ok1 = false, ok2 = false;
        const double lat = kv.captured(1).toDouble(&ok1);
        const double lon = kv.captured(2).toDouble(&ok2);
        if (!ok1 || !ok2 || !inRange(lat, lon))
            return false;
        *latitude = lat;
        *longitude = lon;
        return true;
    }

    QRegularExpressionMatch match = reSimple.match(text);
    if (!match.hasMatch()) {
        match = reLabeled.match(text);
        if (!match.hasMatch())
            return false;
    }

    bool ok1 = false, ok2 = false;
    double lat = match.captured(1).toDouble(&ok1);
    double lon = match.captured(3).toDouble(&ok2);
    if (!ok1 || !ok2)
        return false;

    if (match.captured(2).compare(QStringLiteral("S"), Qt::CaseInsensitive) == 0)
        lat = -lat;
    if (match.captured(4).compare(QStringLiteral("W"), Qt::CaseInsensitive) == 0)
        lon = -lon;

    if (!inRange(lat, lon))
        return false;
    *latitude = lat;
    *longitude = lon;
    return true;
}

bool parseDms(const QString& text, double* latitude, double* longitude)
{
    const auto match = reDms.match(text);
    if (!match.hasMatch())
        return false;

    bool ok = false;
    const double latDeg = match.captured(1).toDouble(&ok);
    if (!ok || latDeg > 90)
        return false;
    const double latMin = match.captured(2).toDouble(&ok);
    if (!ok || latMin >= 60)
        return false;
    const double latSec = match.captured(3).toDouble(&ok);
    if (!ok || latSec >= 60)
        return false;
    const double lonDeg = match.captured(5).toDouble(&ok);
    if (!ok || lonDeg > 180)
        return false;
    const double lonMin = match.captured(6).toDouble(&ok);
    if (!ok || lonMin >= 60)
        return false;
    const double lonSec = match.captured(7).toDouble(&ok);
    if (!ok || lonSec >= 60)
        return false;

    double lat = latDeg + latMin / 60.0 + latSec / 3600.0;
    double lon = lonDeg + lonMin / 60.0 + lonSec / 3600.0;
    if (match.captured(4).compare(QStringLiteral("S"), Qt::CaseInsensitive) == 0)
        lat = -lat;
    if (match.captured(8).compare(QStringLiteral("W"), Qt::CaseInsensitive) == 0)
        lon = -lon;

    if (!inRange(lat, lon))
        return false;
    *latitude = lat;
    *longitude = lon;
    return true;
}

std::vector<GeoPoint> scanRange(const FileSource& source, const LineIndex& index,
                                qint64 first, qint64 end,
                                const QPromise<GeoScanResult>& promise)
{
    std::vector<GeoPoint> points;

    QByteArray scratch;
    const char* base = nullptr;
    quint64 baseOffset = 0;
    if (source.isContiguous()) {
        base = source.data();
    } else {
        baseOffset = index.offsetOf(first);
        const quint64 endOffset = end < index.lineCount()
            ? index.offsetOf(end) : index.fileSize();
        scratch.resize(qsizetype(endOffset - baseOffset));
        source.readInto(baseOffset, scratch.data(), scratch.size());
        base = scratch.constData();
    }

    for (qint64 line = first; line < end; ++line) {
        // Regex per line is slow relative to filtering; honor cancel often.
        if ((line & 4095) == 0 && promise.isCanceled())
            return points;
        const QByteArrayView raw(base + (index.offsetOf(line) - baseOffset),
                                 index.lengthOf(line));
        double lat = 0.0, lon = 0.0;
        if (parseCoordinates(raw, &lat, &lon))
            points.push_back({ qint32(line), lat, lon });
    }
    return points;
}

} // namespace

bool parseCoordinates(QByteArrayView raw, double* latitude, double* longitude)
{
    // Cheap pre-filter: a coordinate needs at least one digit.
    if (std::none_of(raw.begin(), raw.end(),
                     [](char c) { return c >= '0' && c <= '9'; }))
        return false;

    QString text = QString::fromUtf8(raw);
    // Strip a leading timestamp so its digits can't read as coordinates.
    if (const auto ts = reTimestamp.match(text); ts.hasMatch())
        text.remove(0, ts.capturedLength(0));

    return parseDecimalDegrees(text, latitude, longitude)
        || parseDms(text, latitude, longitude);
}

QFuture<GeoScanResult> scanCoordinates(std::shared_ptr<FileSource> source,
                                       std::shared_ptr<const LineIndex> index,
                                       qint64 linesPerChunk)
{
    Q_ASSERT(source && index);
    Q_ASSERT(linesPerChunk > 0);

    return QtConcurrent::run([source, index, linesPerChunk](
                                 QPromise<GeoScanResult>& promise) {
        QElapsedTimer timer;
        timer.start();
        promise.setProgressRange(0, 1000);

        const qint64 total = index->lineCount();
        const int threads = qMax(1, QThread::idealThreadCount());
        const qint64 superChunk = linesPerChunk * threads;

        GeoScanResult result;
        struct Range { qint64 first, end; };
        for (qint64 base = 0; base < total; base += superChunk) {
            if (promise.isCanceled())
                return;
            const qint64 superEnd = std::min(total, base + superChunk);
            QList<Range> ranges;
            for (qint64 s = base; s < superEnd; s += linesPerChunk)
                ranges.append({ s, std::min(superEnd, s + linesPerChunk) });

            const auto chunkPoints = QtConcurrent::blockingMapped(ranges,
                std::function<std::vector<GeoPoint>(const Range&)>(
                    [&](const Range& r) {
                        return scanRange(*source, *index, r.first, r.end,
                                         promise);
                    }));
            for (const auto& v : chunkPoints)
                result.points.insert(result.points.end(), v.begin(), v.end());
            promise.setProgressValue(
                int(superEnd * 1000 / std::max<qint64>(total, 1)));
        }

        result.elapsedMs = timer.elapsed();
        promise.setProgressValue(1000);
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
