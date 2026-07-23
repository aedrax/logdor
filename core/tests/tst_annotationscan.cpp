#include <logdor/AnnotationScan.h>
#include <logdor/LineIndexer.h>

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

QByteArray numberedLines(int first, int count)
{
    QByteArray out;
    for (int i = first; i < first + count; ++i)
        out += "unique line payload number " + QByteArray::number(i) + "\n";
    return out;
}

Annotation annotate(const Opened& o, qint64 start, qint64 end, const QString& note)
{
    Annotation a;
    a.id = QUuid::createUuid();
    a.startLine = start;
    a.endLine = end;
    a.note = note;
    a.createdAt = a.modifiedAt =
        QDateTime(QDate(2026, 7, 23), QTime(12, 0), QTimeZone::utc());
    const LineAnchor anchor = makeAnchor(*o.source, *o.index, start);
    a.anchorHash = anchor.anchorHash;
    a.snippet = anchor.snippet;
    return a;
}

ReanchorResult reanchorSync(AnnotationSet set, const Opened& o,
                            qint64 window = kDefaultReanchorWindowLines)
{
    auto future = reanchorAnnotations(std::move(set), o.source, o.index, window);
    future.waitForFinished();
    return future.result();
}

} // namespace

class tst_AnnotationScan : public QObject {
    Q_OBJECT

private slots:
    void makeAnchorHashesAndSnippets()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "a.log", "short\r\nsecond line\n");
        const LineAnchor a0 = makeAnchor(*o.source, *o.index, 0);
        QCOMPARE(a0.snippet, QStringLiteral("short")); // CR excluded
        QVERIFY(!a0.anchorHash.isEmpty());
        const LineAnchor a1 = makeAnchor(*o.source, *o.index, 1);
        QVERIFY(a0.anchorHash != a1.anchorHash);
    }

    void appendedFileVerifiesInPlace()
    {
        QTemporaryDir dir;
        const QByteArray original = numberedLines(0, 100);
        auto o = openContent(dir, "a.log", original);
        AnnotationSet set;
        set.upsert(annotate(o, 10, 12, "range"));
        set.upsert(annotate(o, 50, 50, "single"));

        auto grown = openContent(dir, "b.log", original + numberedLines(100, 50));
        const ReanchorResult result = reanchorSync(set, grown);
        QCOMPARE(result.verified, 2);
        QCOMPARE(result.reanchored, 0);
        QCOMPARE(result.orphaned, 0);
        QVERIFY(result.set.hasAnnotationAtLine(11));
        QVERIFY(!result.set.isDirty());
    }

    void rotatedFileReanchorsWithUniformDelta()
    {
        QTemporaryDir dir;
        auto original = openContent(dir, "a.log", numberedLines(0, 200));
        AnnotationSet set;
        set.upsert(annotate(original, 120, 122, "first"));
        set.upsert(annotate(original, 150, 150, "second"));
        set.upsert(annotate(original, 180, 180, "third"));

        // Head truncated: the first 100 lines rotated away.
        auto rotated = openContent(dir, "b.log", numberedLines(100, 100));
        const ReanchorResult result = reanchorSync(set, rotated);
        QCOMPARE(result.orphaned, 0);
        QCOMPARE(result.verified + result.reanchored, 3);
        QCOMPARE(result.set.annotations()[0].startLine, qint64(20));
        QCOMPARE(result.set.annotations()[0].endLine, qint64(22));
        QCOMPARE(result.set.annotations()[1].startLine, qint64(50));
        QCOMPARE(result.set.annotations()[2].startLine, qint64(80));
    }

    void vanishedLineIsOrphanedNotDropped()
    {
        QTemporaryDir dir;
        auto original = openContent(dir, "a.log", numberedLines(0, 50));
        AnnotationSet set;
        const Annotation gone = annotate(original, 25, 25, "will vanish");
        set.upsert(gone);
        set.upsert(annotate(original, 10, 10, "stays"));

        // New file lacks line 25's content entirely.
        QByteArray without = numberedLines(0, 25) + numberedLines(26, 24);
        auto changed = openContent(dir, "b.log", without);
        const ReanchorResult result = reanchorSync(set, changed);
        QCOMPARE(result.orphaned, 1);
        QCOMPARE(result.set.size(), 2); // kept, flagged
        QVERIFY(result.set.find(gone.id)->orphaned);
        QCOMPARE(result.set.find(gone.id)->note, QStringLiteral("will vanish"));
    }

    void duplicateLinesNearestToExpectedWins()
    {
        QTemporaryDir dir;
        // Identical content on lines 0, 40, 80.
        QByteArray content;
        for (int i = 0; i < 100; ++i)
            content += (i % 40 == 0) ? QByteArray("duplicate marker line\n")
                                     : "filler " + QByteArray::number(i) + "\n";
        auto o = openContent(dir, "a.log", content);

        AnnotationSet set;
        Annotation a = annotate(o, 40, 40, "middle dup");
        // Force a relocation: pretend it was saved at line 45.
        a.startLine = 45;
        a.endLine = 45;
        set.upsert(a);

        const ReanchorResult result = reanchorSync(set, o);
        QCOMPARE(result.reanchored, 1);
        QCOMPARE(result.set.annotations()[0].startLine, qint64(40)); // not 0/80
    }

    void windowBoundOrphansDistantMatches()
    {
        QTemporaryDir dir;
        auto original = openContent(dir, "a.log", numberedLines(0, 2000));
        AnnotationSet set;
        Annotation a = annotate(original, 1500, 1500, "far away");
        a.startLine = 0; // pretend it was saved at line 0; actual match at 1500
        a.endLine = 0;
        set.upsert(a);

        const ReanchorResult narrow = reanchorSync(set, original, 100);
        QCOMPARE(narrow.orphaned, 1);
        const ReanchorResult wide = reanchorSync(set, original, 2000);
        QCOMPARE(wide.reanchored, 1);
        QCOMPARE(wide.set.annotations()[0].startLine, qint64(1500));
    }

    void cancellation()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "a.log", numberedLines(0, 100000));
        AnnotationSet set;
        for (int i = 0; i < 50; ++i) {
            Annotation a = annotate(o, i * 100, i * 100, "n");
            a.anchorHash = "0000000000000000"; // never matches: full window scans
            set.upsert(a);
        }
        auto future = reanchorAnnotations(std::move(set), o.source, o.index);
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
    }
};

QTEST_APPLESS_MAIN(tst_AnnotationScan)
#include "tst_annotationscan.moc"
