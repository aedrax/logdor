#include "../src/logviewerwidget.h"

#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>

#include <QSignalSpy>
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
