#include <logdor/SortScan.h>

#include <QTest>

using namespace logdor;

namespace {

std::shared_ptr<const ColumnData> stringColumn(const QStringList& values)
{
    ColumnData::Builder builder(FieldType::String);
    for (const QString& value : values)
        builder.appendString(value.toUtf8());
    return std::make_shared<const ColumnData>(std::move(builder).build());
}

std::shared_ptr<const ColumnData> intColumn(const QStringList& values)
{
    ColumnData::Builder builder(FieldType::Integer);
    for (const QString& value : values)
        builder.appendInt(value);
    return std::make_shared<const ColumnData>(std::move(builder).build());
}

SortResult sortSync(RowSet rows, SortKeyKind kind,
                    std::shared_ptr<const ColumnData> keys,
                    std::shared_ptr<const std::vector<quint8>> severity = nullptr)
{
    auto future = sortRows(std::move(rows), kind, std::move(keys),
                           std::move(severity));
    future.waitForFinished();
    return future.result();
}

} // namespace

class tst_SortScan : public QObject {
    Q_OBJECT

private slots:
    void integerSortsNumerically()
    {
        // Lexicographic would give 10 < 9; numeric must not.
        const auto keys = intColumn({ "10", "9", "100", "junk", "2" });
        const auto result = sortSync(RowSet::all(5), SortKeyKind::Integer, keys);
        // junk first (unparseable), then 2, 9, 10, 100.
        QCOMPARE(result.order, (std::vector<qint32>{ 3, 4, 1, 0, 2 }));
    }

    void textSortsLexicographically()
    {
        const auto keys = stringColumn({ "banana", "apple", "cherry", "apple" });
        const auto result = sortSync(RowSet::all(4), SortKeyKind::Text, keys);
        // Stability: the two "apple" rows keep source order (1 before 3).
        QCOMPARE(result.order, (std::vector<qint32>{ 1, 3, 0, 2 }));
    }

    void severitySortsByEnumOrder()
    {
        auto severity = std::make_shared<std::vector<quint8>>(
            std::vector<quint8>{ 5 /*Error*/, 2 /*Debug*/, 6 /*Fatal*/,
                                 2 /*Debug*/, 0 /*None*/ });
        const auto result = sortSync(RowSet::all(5), SortKeyKind::Severity,
                                     nullptr, severity);
        QCOMPARE(result.order, (std::vector<qint32>{ 4, 1, 3, 0, 2 }));
    }

    void subsetRowSetSortsRowSetPositions()
    {
        // Visible rows are source lines {1, 3, 4}; keys live per SOURCE line.
        const auto keys = intColumn({ "0", "30", "0", "10", "20" });
        const RowSet rows = RowSet::fromLines({ 1, 3, 4 }, 5);
        const auto result = sortSync(rows, SortKeyKind::Integer, keys);
        // Positions within the RowSet: pos1 (line 3, 10) < pos2 (line 4, 20)
        // < pos0 (line 1, 30).
        QCOMPARE(result.order, (std::vector<qint32>{ 1, 2, 0 }));
    }

    void emptyRowSet()
    {
        const auto result = sortSync(RowSet(), SortKeyKind::Text,
                                     stringColumn({}));
        QVERIFY(result.order.empty());
    }
};

QTEST_APPLESS_MAIN(tst_SortScan)
#include "tst_sortscan.moc"
