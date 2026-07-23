#include "../src/annotationhub.h"
#include "../src/logtablemodel.h"

#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>

#include <QSignalSpy>

#include <QAbstractItemModelTester>
#include <QTemporaryDir>
#include <QTest>

using namespace logdor;

namespace {

struct Opened {
    std::shared_ptr<FileSource> source;
    std::shared_ptr<const LineIndex> index;
};

Opened openContent(const QTemporaryDir& dir, const QString& name,
                   const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size())
        return {};
    f.close();
    auto source = FileSource::open(path);
    auto future = buildLineIndex(source);
    future.waitForFinished();
    return { source, future.result().index };
}

// Counts parseLine calls to prove the LRU prevents re-parsing.
class CountingParser : public FormatParser {
public:
    mutable std::atomic<int> parseCalls { 0 };

    QString id() const override { return QStringLiteral("counting"); }
    QString displayName() const override { return QStringLiteral("Counting"); }
    QList<FieldSchema> schema() const override
    {
        return { { QStringLiteral("Log"), FieldType::String, FieldHint::Message } };
    }
    void parseLine(QByteArrayView raw, ParsedRow& out) const override
    {
        ++parseCalls;
        out.fields.clear();
        out.fields.append(QString::fromUtf8(raw));
        out.ok = true;
    }
    bool matchesStructure(QByteArrayView) const override { return true; }
};

} // namespace

class tst_LogTableModel : public QObject {
    Q_OBJECT

private slots:
    void modelContractAcrossTransitions()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "m.log", QByteArray("a\nb\nc\nd\ne\n"));

        LogTableModel model;
        QAbstractItemModelTester tester(
            &model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        QCOMPARE(model.rowCount(), 0);
        model.setSource(o.source, o.index, parserById(u"plaintext"));
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(model.columnCount(), 2); // No. + Log

        model.setRowSet(RowSet::fromLines({ 1, 3 }, 5));
        QCOMPARE(model.rowCount(), 2);

        model.setSource(nullptr, nullptr, nullptr); // closed
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.columnCount(), 0);
    }

    void numberColumnShowsSourceLinesUnderFilter()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "n.log", QByteArray("zero\none\ntwo\nthree\n"));

        LogTableModel model;
        model.setSource(o.source, o.index, parserById(u"plaintext"));
        model.setRowSet(RowSet::fromLines({ 1, 3 }, 4));

        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toInt(), 2);
        QCOMPARE(model.data(model.index(0, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("one"));
        QCOMPARE(model.data(model.index(1, 0), Qt::DisplayRole).toInt(), 4);
        QCOMPARE(model.data(model.index(1, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("three"));

        QCOMPARE(model.rowForSourceLine(3), 1);
        QCOMPARE(model.rowForSourceLine(0), -1);
        QCOMPARE(model.sourceLineForRow(1), qint64(3));
        QCOMPARE(model.sourceLineForRow(99), qint64(-1));
    }

    void headersComeFromSchema()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "h.log",
            QByteArray("01-02 03:04:05.678 1 2 I Tag: msg\n"));
        LogTableModel model;
        model.setSource(o.source, o.index, parserById(u"logcat"));
        QCOMPARE(model.columnCount(), 7);
        QCOMPARE(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("No."));
        QCOMPARE(model.headerData(5, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Tag"));
    }

    void severityDrivesRowColors()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "c.log",
            QByteArray("01-02 03:04:05.678 1 2 E Tag: boom\nplain fallback\n"));
        LogTableModel model;
        model.setSource(o.source, o.index, parserById(u"logcat"));

        QCOMPARE(model.data(model.index(0, 1), Qt::BackgroundRole)
                     .value<QColor>(), QColor(255, 99, 71)); // Error = tomato
        QCOMPARE(model.data(model.index(0, 1), Qt::ForegroundRole)
                     .value<QColor>(), QColor(Qt::black));
        // Unknown severity: no brush at all.
        QVERIFY(!model.data(model.index(1, 1), Qt::BackgroundRole).isValid());
    }

    void rowOrderPermutation()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "o.log", QByteArray("a\nb\nc\nd\n"));
        LogTableModel model;
        model.setSource(o.source, o.index, parserById(u"plaintext"));

        // Reverse order: view row 0 shows source line 3.
        model.setRowOrder({ 3, 2, 1, 0 });
        QVERIFY(model.hasRowOrder());
        QCOMPARE(model.sourceLineForRow(0), qint64(3));
        QCOMPARE(model.data(model.index(0, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("d"));
        QCOMPARE(model.rowForSourceLine(3), 0);
        QCOMPARE(model.rowForSourceLine(0), 3);
        // Round trip under permutation.
        for (int row = 0; row < 4; ++row)
            QCOMPARE(model.rowForSourceLine(model.sourceLineForRow(row)), row);

        // Order composes with a filtered row set.
        model.setRowSet(RowSet::fromLines({ 1, 2, 3 }, 4));
        QVERIFY(!model.hasRowOrder()); // setRowSet cleared it
        model.setRowOrder({ 2, 0, 1 }); // rowSet positions
        QCOMPARE(model.sourceLineForRow(0), qint64(3));
        QCOMPARE(model.sourceLineForRow(1), qint64(1));
        QCOMPARE(model.rowForSourceLine(2), 2);
        QCOMPARE(model.rowForSourceLine(0), -1); // hidden stays hidden

        model.setRowOrder({});
        QCOMPARE(model.sourceLineForRow(0), qint64(1));
    }

    void annotationRoles()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "n.log", QByteArray("zero\none\ntwo\n"));
        LogTableModel model;
        model.setSource(o.source, o.index, parserById(u"plaintext"));

        AnnotationHub hub;
        hub.setAuthor(QStringLiteral("paul"));
        hub.beginFile(o.source, o.index, {});
        model.setAnnotationHub(&hub);

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        hub.addAnnotation(1, 1, QStringLiteral("important"),
                          QStringLiteral("#ffd54f"));
        QVERIFY(changed.count() >= 1);

        // Marker only on the No. column of the annotated row.
        QVERIFY(model.data(model.index(1, 0), Qt::DecorationRole).isValid());
        QVERIFY(!model.data(model.index(1, 1), Qt::DecorationRole).isValid());
        QVERIFY(!model.data(model.index(0, 0), Qt::DecorationRole).isValid());

        // Tooltip on every column of the annotated row, nowhere else.
        const QString tooltip =
            model.data(model.index(1, 1), Qt::ToolTipRole).toString();
        QVERIFY(tooltip.contains("important"));
        QVERIFY(tooltip.contains("paul"));
        QVERIFY(!model.data(model.index(0, 1), Qt::ToolTipRole).isValid());
    }

    void lruPreventsReparsing()
    {
        QTemporaryDir dir;
        QByteArray content;
        for (int i = 0; i < 100; ++i)
            content += "line " + QByteArray::number(i) + "\n";
        auto o = openContent(dir, "l.log", content);

        auto counting = std::make_shared<CountingParser>();
        LogTableModel model;
        model.setSource(o.source, o.index, counting);

        for (int pass = 0; pass < 5; ++pass) {
            for (int row = 0; row < 100; ++row) {
                model.data(model.index(row, 1), Qt::DisplayRole);
                model.data(model.index(row, 1), Qt::BackgroundRole);
            }
        }
        QCOMPARE(counting->parseCalls.load(), 100); // one parse per line, ever
    }
};

QTEST_MAIN(tst_LogTableModel)
#include "tst_logtablemodel.moc"
