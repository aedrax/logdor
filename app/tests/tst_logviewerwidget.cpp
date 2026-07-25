#include "../src/logviewerwidget.h"

#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>

#include <QHeaderView>
#include <QSignalSpy>
#include <QTableView>
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

bool waitForScan(LogViewerWidget& widget, int timeoutMs = 5000)
{
    QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
    return spy.wait(timeoutMs);
}

// Append to the file, reopen, extend the index, and deliver the growth to
// the widget the way PluginManager::extendCoreSource would.
Opened growFile(const QString& path, const Opened& old,
                const QByteArray& bytes, LogViewerWidget& widget)
{
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Append)
            || f.write(bytes) != bytes.size())
            return {};
    }
    auto source = FileSource::open(path);
    auto future = extendLineIndex(source, old.index);
    future.waitForFinished();
    const qint64 firstNewLine = old.index->lastLineTerminated()
        ? old.index->lineCount()
        : old.index->lineCount() - 1;
    Opened grown { source, future.result().index };
    widget.extendCoreSource(grown.source, grown.index, firstNewLine);
    return grown;
}

} // namespace

class tst_LogViewerWidget : public QObject {
    Q_OBJECT

private slots:
    void lineConstraintNarrowsAndLifts()
    {
        QTemporaryDir dir;
        QByteArray content;
        for (int i = 0; i < 20; ++i)
            content += "line " + QByteArray::number(i) + "\n";
        auto o = openContent(dir, "c.log", content);

        LogViewerWidget widget;
        widget.setParser(parserById(u"plaintext"));
        widget.setCoreSource(o.source, o.index);
        QCOMPARE(widget.model()->rowCount(), 20);

        // Constrain to three lines.
        {
            QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
            widget.setLineConstraint(
                std::make_shared<std::vector<qint32>>(std::vector<qint32> { 2, 5, 9 }));
            QVERIFY(spy.wait(5000));
        }
        QCOMPARE(widget.model()->rowCount(), 3);
        QCOMPARE(widget.model()->sourceLineForRow(0), qint64(2));
        QCOMPARE(widget.model()->sourceLineForRow(2), qint64(9));

        // Constraint composes with the text filter (AND).
        {
            QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
            widget.applyFilter(FilterOptions(QStringLiteral("line 5")));
            QVERIFY(spy.wait(5000));
        }
        QCOMPARE(widget.model()->rowCount(), 1);
        QCOMPARE(widget.model()->sourceLineForRow(0), qint64(5));

        // Empty payload lifts it (filter still applies).
        {
            QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
            widget.setLineConstraint(
                std::make_shared<std::vector<qint32>>());
            QVERIFY(spy.wait(5000));
        }
        QCOMPARE(widget.model()->rowCount(), 1); // "line 5" text filter alone

        {
            QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
            widget.applyFilter(FilterOptions());
            QVERIFY(spy.wait(5000));
        }
        QCOMPARE(widget.model()->rowCount(), 20);
    }

    void extendPassthroughAppendsInPlace()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "grow.log", "a\nb\nc\n");
        LogViewerWidget widget;
        widget.setParser(parserById(u"plaintext"));
        widget.setCoreSource(o.source, o.index);
        QCOMPARE(widget.model()->rowCount(), 3);

        QSignalSpy inserted(widget.model(), &QAbstractItemModel::rowsInserted);
        QSignalSpy reset(widget.model(), &QAbstractItemModel::modelReset);
        growFile(dir.filePath("grow.log"), o, "d\ne\n", widget);
        QCOMPARE(widget.model()->rowCount(), 5); // synchronous in passthrough
        QCOMPARE(inserted.count(), 1);           // pure append, no reset
        QCOMPARE(reset.count(), 0);
        QCOMPARE(widget.model()->sourceLineForRow(4), qint64(4));
    }

    void extendTextFilterSplicesTail()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "filt.log", "match one\nskip\nmatch two\n");
        LogViewerWidget widget;
        widget.setParser(parserById(u"plaintext"));
        widget.setCoreSource(o.source, o.index);
        widget.applyFilter(FilterOptions(QStringLiteral("match")));
        QVERIFY(waitForScan(widget));
        QCOMPARE(widget.model()->rowCount(), 2);

        growFile(dir.filePath("filt.log"), o, "skip too\nmatch three\n",
                 widget);
        QTRY_COMPARE_WITH_TIMEOUT(widget.model()->rowCount(), 3, 5000);
        QCOMPARE(widget.model()->sourceLineForRow(2), qint64(4));
    }

    void extendReevaluatesUnterminatedLine()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "unterm.log", "one\ntw");
        LogViewerWidget widget;
        widget.setParser(parserById(u"plaintext"));
        widget.setCoreSource(o.source, o.index);
        widget.applyFilter(FilterOptions(QStringLiteral("two")));
        QVERIFY(waitForScan(widget));
        QCOMPARE(widget.model()->rowCount(), 0); // "tw" does not match yet

        growFile(dir.filePath("unterm.log"), o, "o\nother\n", widget);
        QTRY_COMPARE_WITH_TIMEOUT(widget.model()->rowCount(), 1, 5000);
        QCOMPARE(widget.model()->sourceLineForRow(0), qint64(1)); // now "two"
    }

    void extendQueryModeSplicesColumns()
    {
        QTemporaryDir dir;
        const QByteArray head
            = "01-01 10:00:00.000 100 1 E Tag: boom\n"
              "01-01 10:00:01.000 100 1 I Tag: fine\n"
              "01-01 10:00:02.000 100 1 E Tag: boom again\n";
        auto o = openContent(dir, "q.log", head);
        LogViewerWidget widget;
        widget.setParser(parserById(u"logcat"));
        widget.setCoreSource(o.source, o.index);
        widget.applyFilter(FilterOptions(QStringLiteral("level:error"), 0, 0,
                                         Qt::CaseInsensitive, false,
                                         /*queryMode=*/true));
        QVERIFY(waitForScan(widget));
        QCOMPARE(widget.model()->rowCount(), 2);

        growFile(dir.filePath("q.log"), o,
                 "01-01 10:00:03.000 100 1 I Tag: quiet\n"
                 "01-01 10:00:04.000 100 1 E Tag: boom three\n",
                 widget);
        QTRY_COMPARE_WITH_TIMEOUT(widget.model()->rowCount(), 3, 5000);
        QCOMPARE(widget.model()->sourceLineForRow(2), qint64(4));
    }

    void viewStateRoundTrip()
    {
        QTemporaryDir dir;
        QByteArray content;
        for (int i = 0; i < 50; ++i)
            content += "line " + QByteArray::number(i) + "\n";
        auto o = openContent(dir, "r.log", content);

        LogViewerWidget widget;
        widget.setParser(parserById(u"plaintext"));
        widget.setCoreSource(o.source, o.index);
        {
            QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
            widget.applyFilter(FilterOptions());
            QVERIFY(spy.wait(5000));
        }

        // Select two lines and sort by No. descending.
        widget.selectSourceLines({ 7, 8 });
        auto* header = widget.tableView()->horizontalHeader();
        emit header->sectionClicked(0); // ascending (natural, synchronous)
        emit header->sectionClicked(0); // descending
        QTRY_VERIFY(widget.model()->hasRowOrder());
        QCOMPARE(widget.model()->sourceLineForRow(0), qint64(49));

        const QJsonObject state = widget.saveViewState();
        QVERIFY(state.contains(QLatin1String("selection")));
        QCOMPARE(state.value(QLatin1String("sortColumn")).toInt(), 0);

        // Close the file: everything view-related is wiped.
        widget.setCoreSource(nullptr, nullptr);
        QCOMPARE(widget.model()->rowCount(), 0);

        // Reopen and restore; the pending state applies when the scan lands.
        widget.setCoreSource(o.source, o.index);
        widget.restoreViewState(state);
        {
            QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
            widget.applyFilter(FilterOptions());
            QVERIFY(spy.wait(5000));
        }
        QTRY_VERIFY(widget.model()->hasRowOrder()); // sort restored
        QCOMPARE(widget.model()->sourceLineForRow(0), qint64(49));

        QList<int> selected;
        const auto rows = widget.tableView()->selectionModel()->selectedRows();
        for (const QModelIndex& index : rows)
            selected.append(int(widget.model()->sourceLineForRow(index.row())));
        std::sort(selected.begin(), selected.end());
        QCOMPARE(selected, QList<int>({ 7, 8 }));
    }

    void staleRestoreClearedByNewFile()
    {
        QTemporaryDir dir;
        auto a = openContent(dir, "a.log", QByteArray("x\ny\nz\n"));
        auto b = openContent(dir, "b.log", QByteArray("1\n2\n3\n4\n"));

        LogViewerWidget widget;
        widget.setParser(parserById(u"plaintext"));
        widget.setCoreSource(a.source, a.index);

        QJsonObject state;
        state.insert(QStringLiteral("sortColumn"), 0);
        state.insert(QStringLiteral("sortOrder"), int(Qt::DescendingOrder));
        widget.restoreViewState(state);

        // The switch must drop the pending restore before b's first scan.
        widget.setCoreSource(b.source, b.index);
        {
            QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
            widget.applyFilter(FilterOptions());
            QVERIFY(spy.wait(5000));
        }
        QVERIFY(!widget.model()->hasRowOrder());
        QCOMPARE(widget.model()->sourceLineForRow(0), qint64(0));
    }

    void constraintClearedOnNewFile()
    {
        QTemporaryDir dir;
        auto a = openContent(dir, "a.log", QByteArray("x\ny\nz\n"));
        auto b = openContent(dir, "b.log", QByteArray("1\n2\n3\n4\n"));

        LogViewerWidget widget;
        widget.setParser(parserById(u"plaintext"));
        widget.setCoreSource(a.source, a.index);
        {
            QSignalSpy spy(&widget, &LogViewerWidget::filterApplied);
            widget.setLineConstraint(
                std::make_shared<std::vector<qint32>>(std::vector<qint32> { 0 }));
            QVERIFY(spy.wait(5000));
        }
        QCOMPARE(widget.model()->rowCount(), 1);

        widget.setCoreSource(b.source, b.index);
        QCOMPARE(widget.model()->rowCount(), 4); // constraint gone
    }
};

QTEST_MAIN(tst_LogViewerWidget)
#include "tst_logviewerwidget.moc"
