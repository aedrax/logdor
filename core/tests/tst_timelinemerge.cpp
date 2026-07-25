#include <logdor/TimelineMerge.h>

#include <QTest>

using namespace logdor;

namespace {

// A DateTime column built directly from (text, epoch-valid) pairs; text is
// irrelevant to the merge, which reads only the integer lane.
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

std::vector<TimelineRow> merged(std::vector<TimelineInput> inputs,
                                std::vector<qint64>* dropped = nullptr)
{
    auto future = mergeTimeline(std::move(inputs));
    future.waitForFinished();
    TimelineMergeResult result = future.result();
    if (dropped)
        *dropped = result.droppedPerInput;
    return result.order;
}

bool rowEquals(const TimelineRow& row, qint32 fileId, qint32 line)
{
    return row.fileId == fileId && row.line == line;
}

} // namespace

class tst_TimelineMerge : public QObject {
    Q_OBJECT

private slots:
    void interleavesAcrossFiles()
    {
        // File 0 at t=10,30,50; file 1 at t=20,40.
        auto t0 = timeColumn({ { 10, true }, { 30, true }, { 50, true } });
        auto t1 = timeColumn({ { 20, true }, { 40, true } });

        const auto order = merged({ { 0, RowSet::all(3), t0 },
                                    { 1, RowSet::all(2), t1 } });
        QCOMPARE(order.size(), size_t(5));
        QVERIFY(rowEquals(order[0], 0, 0)); // 10
        QVERIFY(rowEquals(order[1], 1, 0)); // 20
        QVERIFY(rowEquals(order[2], 0, 1)); // 30
        QVERIFY(rowEquals(order[3], 1, 1)); // 40
        QVERIFY(rowEquals(order[4], 0, 2)); // 50
    }

    void tiesAreStableByFileThenLine()
    {
        auto t0 = timeColumn({ { 100, true }, { 100, true } });
        auto t1 = timeColumn({ { 100, true } });

        const auto order = merged({ { 7, RowSet::all(2), t0 },
                                    { 3, RowSet::all(1), t1 } });
        QCOMPARE(order.size(), size_t(3));
        QVERIFY(rowEquals(order[0], 3, 0)); // lower fileId first
        QVERIFY(rowEquals(order[1], 7, 0));
        QVERIFY(rowEquals(order[2], 7, 1));
    }

    void outOfOrderTimestampsSortGlobally()
    {
        // Real logs are not perfectly ordered within a file.
        auto t0 = timeColumn({ { 30, true }, { 10, true }, { 20, true } });
        const auto order = merged({ { 0, RowSet::all(3), t0 } });
        QCOMPARE(order.size(), size_t(3));
        QVERIFY(rowEquals(order[0], 0, 1)); // 10
        QVERIFY(rowEquals(order[1], 0, 2)); // 20
        QVERIFY(rowEquals(order[2], 0, 0)); // 30
    }

    void invalidEpochsDroppedAndCounted()
    {
        auto t0 = timeColumn({ { 10, true }, { 0, false }, { 30, true } });
        auto t1 = timeColumn({ { 0, false } });

        std::vector<qint64> dropped;
        const auto order = merged({ { 0, RowSet::all(3), t0 },
                                    { 1, RowSet::all(1), t1 } },
                                  &dropped);
        QCOMPARE(order.size(), size_t(2));
        QVERIFY(rowEquals(order[0], 0, 0));
        QVERIFY(rowEquals(order[1], 0, 2));
        QCOMPARE(dropped, (std::vector<qint64>{ 1, 1 }));
    }

    void explicitRowSetsMergeOnlyVisibleRows()
    {
        // Only lines 0 and 2 of file 0 are visible (filtered).
        auto t0 = timeColumn({ { 10, true }, { 20, true }, { 30, true } });
        auto t1 = timeColumn({ { 15, true } });

        const auto order = merged({ { 0, RowSet::fromLines({ 0, 2 }, 3), t0 },
                                    { 1, RowSet::all(1), t1 } });
        QCOMPARE(order.size(), size_t(3));
        QVERIFY(rowEquals(order[0], 0, 0)); // 10
        QVERIFY(rowEquals(order[1], 1, 0)); // 15
        QVERIFY(rowEquals(order[2], 0, 2)); // 30; line 1 hidden
    }

    void emptyAndNoInputs()
    {
        QCOMPARE(merged({}).size(), size_t(0));

        auto t0 = timeColumn({});
        const auto order = merged({ { 0, RowSet::all(0), t0 } });
        QCOMPARE(order.size(), size_t(0));
    }

    void singleFileDegeneratesToTimeSort()
    {
        auto t0 = timeColumn({ { 5, true }, { 1, true }, { 3, true } });
        const auto order = merged({ { 0, RowSet::all(3), t0 } });
        QCOMPARE(order.size(), size_t(3));
        QVERIFY(rowEquals(order[0], 0, 1));
        QVERIFY(rowEquals(order[1], 0, 2));
        QVERIFY(rowEquals(order[2], 0, 0));
    }

    void cancellationProducesNoResult()
    {
        std::vector<std::pair<qint64, bool>> epochs;
        epochs.reserve(500000);
        for (int i = 0; i < 500000; ++i)
            epochs.push_back({ (qint64(i) * 7919) % 1000003, true });
        auto t0 = timeColumn(epochs);

        auto future = mergeTimeline({ { 0, RowSet::all(500000), t0 } });
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        // A cancelled future holds no result; resultCount stays 0.
        QCOMPARE(future.resultCount(), 0);
    }
};

QTEST_APPLESS_MAIN(tst_TimelineMerge)
#include "tst_timelinemerge.moc"
