#include <logdor/HistogramScan.h>

#include <QTest>

using namespace logdor;

namespace {

// A DateTime column from (epochMs, valid) pairs; only the integer lane is
// read by the scan.
std::shared_ptr<const ColumnData> timeColumn(
    const std::vector<std::pair<qint64, bool>>& epochs)
{
    ColumnData::Builder builder(FieldType::DateTime);
    for (const auto& [ms, valid] : epochs) {
        builder.blob.append("t");
        builder.offsets.push_back(quint64(builder.blob.size()));
        builder.ints.push_back(valid ? ms : 0);
        builder.intValid.push_back(valid);
    }
    return std::make_shared<const ColumnData>(std::move(builder).build());
}

HistogramResult scan(RowSet rows, std::shared_ptr<const ColumnData> time,
                     std::shared_ptr<const std::vector<quint8>> severity = {},
                     HistogramRequest request = {})
{
    auto future = scanHistogram(std::move(rows), std::move(time),
                                std::move(severity), request);
    future.waitForFinished();
    return future.result();
}

qint64 bucketTotal(const HistogramResult& result, size_t bucket)
{
    qint64 sum = 0;
    for (qint64 count : result.buckets[bucket])
        sum += count;
    return sum;
}

qint64 grandTotal(const HistogramResult& result)
{
    qint64 sum = 0;
    for (size_t i = 0; i < result.buckets.size(); ++i)
        sum += bucketTotal(result, i);
    return sum;
}

} // namespace

class tst_HistogramScan : public QObject {
    Q_OBJECT

private slots:
    void autoRangeBinsEverything()
    {
        // 100 rows at t = 0..990 step 10.
        std::vector<std::pair<qint64, bool>> epochs;
        for (int i = 0; i < 100; ++i)
            epochs.push_back({ i * 10, true });
        const auto result
            = scan(RowSet::all(100), timeColumn(epochs), {}, { 0, 0, 10 });

        QCOMPARE(result.minMs, qint64(0));
        QCOMPARE(result.maxMs, qint64(990));
        QCOMPARE(result.fromMs, qint64(0));
        QCOMPARE(result.toMs, qint64(990));
        QCOMPARE(result.invalidRows, qint64(0));
        QCOMPARE(grandTotal(result), qint64(100));
        // width = 990/10 + 1 = 100: exactly 10 values per bucket.
        QCOMPARE(result.bucketWidthMs, qint64(100));
        for (size_t i = 0; i < 10; ++i)
            QCOMPARE(bucketTotal(result, i), qint64(10));
    }

    void lastBucketIncludesRangeEnd()
    {
        const auto result = scan(RowSet::all(3),
                                 timeColumn({ { 0, true },
                                              { 500, true },
                                              { 1000, true } }),
                                 {}, { 0, 0, 4 });
        QCOMPARE(grandTotal(result), qint64(3));
        QCOMPARE(bucketTotal(result, result.buckets.size() - 1), qint64(1));
    }

    void severitySplitAndNullSeverity()
    {
        auto time = timeColumn({ { 10, true }, { 20, true }, { 30, true } });
        auto severity = std::make_shared<std::vector<quint8>>(
            std::vector<quint8> { quint8(Severity::Error),
                                  quint8(Severity::Error),
                                  quint8(Severity::Info) });
        const auto split
            = scan(RowSet::all(3), time, severity, { 0, 0, 1 });
        QCOMPARE(split.buckets[0][size_t(Severity::Error)], qint64(2));
        QCOMPARE(split.buckets[0][size_t(Severity::Info)], qint64(1));
        QCOMPARE(split.buckets[0][size_t(Severity::None)], qint64(0));

        const auto plain = scan(RowSet::all(3), time, nullptr, { 0, 0, 1 });
        QCOMPARE(plain.buckets[0][size_t(Severity::None)], qint64(3));
    }

    void invalidRowsCountedNotBinned()
    {
        const auto result = scan(RowSet::all(4),
                                 timeColumn({ { 10, true },
                                              { 0, false },
                                              { 0, false },
                                              { 20, true } }));
        QCOMPARE(result.invalidRows, qint64(2));
        QCOMPARE(grandTotal(result), qint64(2));
    }

    void explicitRangeIgnoresOutsiders()
    {
        const auto result = scan(RowSet::all(5),
                                 timeColumn({ { 5, true },
                                              { 100, true },
                                              { 150, true },
                                              { 200, true },
                                              { 900, true } }),
                                 {}, { 100, 200, 4 });
        QCOMPARE(result.fromMs, qint64(100));
        QCOMPARE(result.toMs, qint64(200));
        QCOMPARE(grandTotal(result), qint64(3)); // 5 and 900 ignored
        // The observed span still reports the full data.
        QCOMPARE(result.minMs, qint64(5));
        QCOMPARE(result.maxMs, qint64(900));
    }

    void subsetRowSetScansOnlyVisible()
    {
        const auto result = scan(RowSet::fromLines({ 0, 2 }, 4),
                                 timeColumn({ { 10, true },
                                              { 20, true },
                                              { 30, true },
                                              { 40, true } }));
        QCOMPARE(grandTotal(result), qint64(2));
        QCOMPARE(result.minMs, qint64(10));
        QCOMPARE(result.maxMs, qint64(30));
    }

    void emptyAndAllInvalid()
    {
        const auto empty = scan(RowSet::all(0), timeColumn({}));
        QVERIFY(empty.minMs > empty.maxMs);
        QCOMPARE(grandTotal(empty), qint64(0));

        const auto invalid
            = scan(RowSet::all(2),
                   timeColumn({ { 0, false }, { 0, false } }));
        QVERIFY(invalid.minMs > invalid.maxMs);
        QCOMPARE(invalid.invalidRows, qint64(2));
        QCOMPARE(grandTotal(invalid), qint64(0));
    }

    void singleTimestampDegenerates()
    {
        const auto result
            = scan(RowSet::all(2),
                   timeColumn({ { 777, true }, { 777, true } }), {},
                   { 0, 0, 512 });
        QCOMPARE(result.fromMs, qint64(777));
        QCOMPARE(result.toMs, qint64(777));
        QCOMPARE(result.bucketWidthMs, qint64(1));
        QCOMPARE(bucketTotal(result, 0), qint64(2));
        QCOMPARE(grandTotal(result), qint64(2));
    }

    void cancellationProducesNoResult()
    {
        std::vector<std::pair<qint64, bool>> epochs;
        epochs.reserve(3'000'000);
        for (qint64 i = 0; i < 3'000'000; ++i)
            epochs.push_back({ i, true });
        auto future = scanHistogram(RowSet::all(3'000'000),
                                    timeColumn(epochs), nullptr, {});
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
    }
};

QTEST_APPLESS_MAIN(tst_HistogramScan)
#include "tst_histogramscan.moc"
