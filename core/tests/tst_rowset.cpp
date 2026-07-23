#include <logdor/RowSet.h>

#include <QTest>

using logdor::RowSet;

class tst_RowSet : public QObject {
    Q_OBJECT

private slots:
    void emptyByDefault()
    {
        RowSet set;
        QCOMPARE(set.size(), 0);
        QVERIFY(!set.isAll());
        QCOMPARE(set.rowForSourceLine(0), -1);
    }

    void allIsIdentityWithoutAllocation()
    {
        const RowSet set = RowSet::all(10'000'000);
        QVERIFY(set.isAll());
        QCOMPARE(set.size(), 10'000'000);
        QCOMPARE(set.sourceLine(1234567), 1234567);
        QCOMPARE(set.rowForSourceLine(9'999'999), 9'999'999);
        QCOMPARE(set.rowForSourceLine(10'000'000), -1);
        QCOMPARE(set.rowForSourceLine(-1), -1);
        QCOMPARE(set.memoryUsage(), size_t(0));
    }

    void explicitMapsAndSearches()
    {
        const RowSet set = RowSet::fromLines({ 2, 5, 9 }, 12);
        QVERIFY(!set.isAll());
        QCOMPARE(set.size(), 3);
        QCOMPARE(set.lineCount(), 12);
        QCOMPARE(set.sourceLine(0), 2);
        QCOMPARE(set.sourceLine(2), 9);
        QCOMPARE(set.rowForSourceLine(5), 1);
        QCOMPARE(set.rowForSourceLine(6), -1);
        QCOMPARE(set.rowForSourceLine(11), -1);
        QVERIFY(set.memoryUsage() >= 3 * sizeof(qint32));
    }

    void completeSetCollapsesToAll()
    {
        const RowSet set = RowSet::fromLines({ 0, 1, 2, 3 }, 4);
        QVERIFY(set.isAll());
        QCOMPARE(set.memoryUsage(), size_t(0));
    }
};

QTEST_APPLESS_MAIN(tst_RowSet)
#include "tst_rowset.moc"
