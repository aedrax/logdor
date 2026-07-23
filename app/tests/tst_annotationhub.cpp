#include "../src/annotationhub.h"

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

} // namespace

class tst_AnnotationHub : public QObject {
    Q_OBJECT

private slots:
    void addStampsEverything()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "a.log", "alpha\nbravo\ncharlie\n");
        AnnotationHub hub;
        hub.setAuthor(QStringLiteral("paul"));
        hub.beginFile(o.source, o.index, {});

        QSignalSpy changed(&hub, &AnnotationHub::annotationsChanged);
        const QUuid id = hub.addAnnotation(1, 2, QStringLiteral("note"),
                                           QStringLiteral("#ffcc00"),
                                           QStringLiteral("triage"));
        QVERIFY(!id.isNull());
        QCOMPARE(changed.count(), 1);

        const Annotation* a = hub.set().find(id);
        QVERIFY(a);
        QCOMPARE(a->author, QStringLiteral("paul"));
        QCOMPARE(a->snippet, QStringLiteral("bravo"));
        QVERIFY(!a->anchorHash.isEmpty());
        QVERIFY(a->createdAt.isValid());
        QCOMPARE(a->createdAt, a->modifiedAt);
        QVERIFY(hub.isDirty());

        // Out-of-range adds fail.
        QVERIFY(hub.addAnnotation(99, 99, "x").isNull());
        QVERIFY(hub.addAnnotation(-1, 0, "x").isNull());
    }

    void updateRestampsModified()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "a.log", "alpha\nbravo\n");
        AnnotationHub hub;
        hub.beginFile(o.source, o.index, {});
        const QUuid id = hub.addAnnotation(0, 0, QStringLiteral("v1"));
        const QDateTime created = hub.set().find(id)->modifiedAt;

        Annotation edited = *hub.set().find(id);
        edited.note = QStringLiteral("v2");
        QTest::qWait(2); // ensure the clock moves
        QVERIFY(hub.updateAnnotation(edited));
        QCOMPARE(hub.set().find(id)->note, QStringLiteral("v2"));
        QVERIFY(hub.set().find(id)->modifiedAt >= created);

        Annotation unknown = edited;
        unknown.id = QUuid::createUuid();
        QVERIFY(!hub.updateAnnotation(unknown));
        QVERIFY(hub.removeAnnotation(id));
        QVERIFY(!hub.removeAnnotation(id));
    }

    void mergeFromEmitsOnceAndReanchors()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "a.log", "alpha\nbravo\ncharlie\n");
        AnnotationHub hub;
        hub.beginFile(o.source, o.index, {});
        hub.addAnnotation(0, 0, QStringLiteral("mine"));

        // Colleague's set with a valid anchor for line 2.
        AnnotationSet theirs;
        Annotation imported;
        imported.id = QUuid::createUuid();
        imported.startLine = 2;
        imported.endLine = 2;
        imported.note = QStringLiteral("theirs");
        imported.createdAt = imported.modifiedAt = QDateTime::currentDateTimeUtc();
        const LineAnchor anchor = makeAnchor(*o.source, *o.index, 2);
        imported.anchorHash = anchor.anchorHash;
        imported.snippet = anchor.snippet;
        theirs.upsert(imported);

        QSignalSpy changed(&hub, &AnnotationHub::annotationsChanged);
        QSignalSpy reanchored(&hub, &AnnotationHub::reanchorFinished);
        hub.mergeFrom(theirs);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(hub.set().size(), 2);
        QVERIFY(reanchored.wait(5000));
        QCOMPARE(reanchored.first()[0].toInt() + reanchored.first()[1].toInt(), 2);
        QCOMPARE(reanchored.first()[2].toInt(), 0); // nothing orphaned
    }

    void lineTextAndClear()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "a.log", "alpha\nbravo\n");
        AnnotationHub hub;
        hub.beginFile(o.source, o.index, {});
        QCOMPARE(hub.lineText(1), QStringLiteral("bravo"));
        QCOMPARE(hub.lineText(5), QString());
        QVERIFY(hub.hasFile());
        QVERIFY(hub.identity().isValid());

        hub.clear();
        QVERIFY(!hub.hasFile());
        QVERIFY(hub.set().isEmpty());
        QCOMPARE(hub.lineText(0), QString());
    }
};

QTEST_MAIN(tst_AnnotationHub)
#include "tst_annotationhub.moc"
