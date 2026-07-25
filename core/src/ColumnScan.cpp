#include "logdor/ColumnScan.h"

#include <QElapsedTimer>
#include <QThread>
#include <QtConcurrentMap>
#include <QtConcurrentRun>

#include <algorithm>

namespace logdor {

namespace {

struct ChunkShard {
    QList<ColumnData::Builder> builders; // parallel to requested columns
    std::vector<quint8> severity;
};

ChunkShard scanChunk(const FileSource& source, const LineIndex& index,
                     const FormatParser& parser, const QList<int>& columns,
                     const QList<FieldType>& columnTypes,
                     const QList<TimestampCodec>& columnCodecs,
                     bool wantSeverity, qint64 first, qint64 end,
                     const QPromise<ColumnScanResult>& promise)
{
    ChunkShard shard;
    shard.builders.reserve(columns.size());
    for (int i = 0; i < columnTypes.size(); ++i)
        shard.builders.emplace_back(columnTypes[i], columnCodecs[i]);
    if (wantSeverity)
        shard.severity.reserve(size_t(end - first));

    // Buffered sources: one bulk read per chunk (same pattern as scanRange).
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

    ParsedRow row;
    for (qint64 line = first; line < end; ++line) {
        // Parsing is much slower than filtering; honor cancel mid-chunk so
        // latency stays bounded by ~4k parses, not a whole super-chunk.
        // A truncated shard is fine - a cancelled scan discards everything.
        if ((line & 4095) == 0 && promise.isCanceled())
            return shard;
        const QByteArrayView raw(base + (index.offsetOf(line) - baseOffset),
                                 index.lengthOf(line));
        parser.parseLine(raw, row);
        for (int i = 0; i < columns.size(); ++i)
            shard.builders[i].append(row.fields[columns[i]], columnTypes[i]);
        if (wantSeverity)
            shard.severity.push_back(quint8(row.severity));
    }
    return shard;
}

// Merge per-chunk shards, in order, into final columns.
ColumnScanResult mergeShards(QList<ChunkShard>&& shards,
                             const QList<int>& columns,
                             const QList<FieldType>& columnTypes,
                             const QList<TimestampCodec>& columnCodecs,
                             bool wantSeverity, qint64 totalLines)
{
    ColumnScanResult result;

    for (int i = 0; i < columns.size(); ++i) {
        // The codec carries the monotonic-time flag into the built column.
        ColumnData::Builder merged(columnTypes[i], columnCodecs[i]);
        // DateTime columns carry both lanes: text blob + parsed epochs.
        if (columnTypes[i] != FieldType::String) {
            merged.ints.reserve(size_t(totalLines));
            merged.intValid.reserve(size_t(totalLines));
            for (ChunkShard& shard : shards) {
                auto& b = shard.builders[i];
                merged.ints.insert(merged.ints.end(), b.ints.begin(), b.ints.end());
                merged.intValid.insert(merged.intValid.end(),
                                       b.intValid.begin(), b.intValid.end());
            }
        }
        if (columnTypes[i] != FieldType::Integer) {
            qsizetype blobSize = 0;
            size_t lineCount = 0;
            for (const ChunkShard& shard : shards) {
                blobSize += shard.builders[i].blob.size();
                lineCount += shard.builders[i].offsets.size() - 1;
            }
            merged.blob.reserve(blobSize);
            merged.offsets.reserve(lineCount + 1);
            for (ChunkShard& shard : shards) {
                const auto& b = shard.builders[i];
                const quint64 rebase = quint64(merged.blob.size());
                merged.blob.append(b.blob);
                // offsets[0] is always 0; skip it on append.
                for (size_t k = 1; k < b.offsets.size(); ++k)
                    merged.offsets.push_back(b.offsets[k] + rebase);
            }
        }
        result.columns.insert(
            columns[i],
            std::make_shared<const ColumnData>(std::move(merged).build()));
    }

    if (wantSeverity) {
        auto severity = std::make_shared<std::vector<quint8>>();
        severity->reserve(size_t(totalLines));
        for (const ChunkShard& shard : shards)
            severity->insert(severity->end(), shard.severity.begin(),
                             shard.severity.end());
        result.severity = std::move(severity);
    }
    return result;
}

// Resolve the codec of each requested DateTime column: the declared
// timeFormat wins; otherwise detect from the first parsed sample values.
// Runs once, before chunking - every shard must use the same codec.
QList<TimestampCodec> resolveCodecs(const FileSource& source,
                                    const LineIndex& index,
                                    const FormatParser& parser,
                                    const QList<FieldSchema>& schema,
                                    const QList<int>& columns,
                                    const TimeParseContext& timeContext)
{
    QList<TimestampCodec> codecs;
    codecs.reserve(columns.size());
    QList<int> pending; // indices into columns needing detection
    for (int i = 0; i < columns.size(); ++i) {
        const FieldSchema& field = schema[columns[i]];
        codecs.append(field.type == FieldType::DateTime
                          ? TimestampCodec::fromFormatString(field.timeFormat,
                                                             timeContext)
                          : TimestampCodec());
        if (field.type == FieldType::DateTime && !codecs[i].isValid())
            pending.append(i);
    }
    if (pending.isEmpty())
        return codecs;

    constexpr qint64 kMaxSampleLines = 2048;
    constexpr qsizetype kMaxSamples = 256;
    QList<QStringList> samples(pending.size());
    ParsedRow row;
    const qint64 end = std::min(index.lineCount(), kMaxSampleLines);
    for (qint64 line = 0; line < end; ++line) {
        const QByteArray raw
            = source.read(index.offsetOf(line), index.lengthOf(line));
        parser.parseLine(QByteArrayView(raw), row);
        bool wantMore = false;
        for (int p = 0; p < pending.size(); ++p) {
            const QString& value = row.fields[columns[pending[p]]];
            if (samples[p].size() < kMaxSamples) {
                if (!value.isEmpty())
                    samples[p].append(value);
                wantMore = wantMore || samples[p].size() < kMaxSamples;
            }
        }
        if (!wantMore)
            break;
    }
    for (int p = 0; p < pending.size(); ++p)
        codecs[pending[p]] = TimestampCodec::detect(samples[p], timeContext);
    return codecs;
}

} // namespace

QFuture<ColumnScanResult> extractColumns(
    std::shared_ptr<FileSource> source,
    std::shared_ptr<const LineIndex> index,
    std::shared_ptr<const FormatParser> parser,
    QList<int> columns, bool wantSeverity, TimeParseContext timeContext,
    qint64 linesPerChunk)
{
    Q_ASSERT(source && index && parser);
    Q_ASSERT(linesPerChunk > 0);

    return QtConcurrent::run([source, index, parser,
                              columns = std::move(columns), wantSeverity,
                              timeContext = std::move(timeContext),
                              linesPerChunk](QPromise<ColumnScanResult>& promise) {
        QElapsedTimer timer;
        timer.start();
        promise.setProgressRange(0, 1000);

        const auto schema = parser->schema();
        QList<FieldType> columnTypes;
        columnTypes.reserve(columns.size());
        for (int col : columns)
            columnTypes.append(schema[col].type);
        const QList<TimestampCodec> columnCodecs = resolveCodecs(
            *source, *index, *parser, schema, columns, timeContext);

        const qint64 total = index->lineCount();
        const int threads = qMax(1, QThread::idealThreadCount());
        const qint64 superChunk = linesPerChunk * threads;
        QList<ChunkShard> shards;

        struct Range { qint64 first, end; };
        for (qint64 base = 0; base < total; base += superChunk) {
            if (promise.isCanceled())
                return;
            const qint64 superEnd = std::min(total, base + superChunk);
            QList<Range> ranges;
            for (qint64 s = base; s < superEnd; s += linesPerChunk)
                ranges.append({ s, std::min(superEnd, s + linesPerChunk) });

            auto chunkShards = QtConcurrent::blockingMapped(ranges,
                std::function<ChunkShard(const Range&)>([&](const Range& r) {
                    return scanChunk(*source, *index, *parser, columns,
                                     columnTypes, columnCodecs, wantSeverity,
                                     r.first, r.end, promise);
                }));
            for (auto& shard : chunkShards)
                shards.append(std::move(shard));
            promise.setProgressValue(int(superEnd * 1000 / std::max<qint64>(total, 1)));
        }

        ColumnScanResult result = mergeShards(std::move(shards), columns,
                                              columnTypes, columnCodecs,
                                              wantSeverity, total);
        result.elapsedMs = timer.elapsed();
        promise.setProgressValue(1000);
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
