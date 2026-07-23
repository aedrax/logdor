#include <logdor/Annotation.h>

#include <QTest>

using namespace logdor;

namespace {

Annotation makeAnnotation(qint64 start, qint64 end, const QString& note,
                          const QDateTime& modified = QDateTime(),
                          const QUuid& id = QUuid())
{
    Annotation a;
    a.id = id.isNull() ? QUuid::createUuid() : id;
    a.startLine = start;
    a.endLine = end;
    a.note = note;
    a.author = QStringLiteral("tester");
    a.createdAt = QDateTime(QDate(2026, 7, 23), QTime(10, 0), QTimeZone::utc());
    a.modifiedAt = modified.isValid() ? modified : a.createdAt;
    a.anchorHash = "abc123";
    a.snippet = QStringLiteral("snippet of ") + note;
    return a;
}

FileIdentity testIdentity()
{
    FileIdentity identity;
    identity.size = 1234;
    identity.prefixSha256 = "deadbeef";
    identity.prefixLength = 1234;
    return identity;
}

} // namespace

class tst_Annotation : public QObject {
    Q_OBJECT

private slots:
    void setKeepsSortedAndQueries()
    {
        AnnotationSet set;
        const auto late = makeAnnotation(100, 102, "late");
        const auto early = makeAnnotation(5, 5, "early");
        QVERIFY(set.upsert(late));
        QVERIFY(set.upsert(early));
        QCOMPARE(set.size(), 2);
        QCOMPARE(set.annotations()[0].note, QStringLiteral("early"));

        QVERIFY(set.hasAnnotationAtLine(5));
        QVERIFY(set.hasAnnotationAtLine(101)); // inside the range
        QVERIFY(!set.hasAnnotationAtLine(4));
        QVERIFY(!set.hasAnnotationAtLine(103));
        QCOMPARE(set.annotationsAtLine(102).size(), 1);
        QCOMPARE(set.annotationsAtLine(6).size(), 0);

        // Overlapping annotations both report.
        set.upsert(makeAnnotation(101, 105, "overlap"));
        QCOMPARE(set.annotationsAtLine(101).size(), 2);

        QVERIFY(set.remove(early.id));
        QVERIFY(!set.remove(early.id));
        QVERIFY(!set.hasAnnotationAtLine(5));

        // upsert by same id replaces, not duplicates.
        Annotation edited = late;
        edited.note = QStringLiteral("edited");
        set.upsert(edited);
        QCOMPARE(set.annotationsAtLine(100).size(), 1);
        QCOMPARE(set.find(late.id)->note, QStringLiteral("edited"));
        QVERIFY(!set.upsert(Annotation {})); // null id rejected
    }

    void roundTripPreservesEverything()
    {
        AnnotationSet set;
        auto colored = makeAnnotation(3, 7, "range note");
        colored.color = QStringLiteral("#ffcc00");
        colored.tag = QStringLiteral("triage");
        set.upsert(colored);
        set.upsert(makeAnnotation(0, 0, "first line"));

        const QByteArray bytes = saveAnnotations(set, testIdentity(),
                                                 QStringLiteral("app.log"));
        AnnotationFileError error;
        const auto loaded = loadAnnotations(bytes, &error);
        QVERIFY2(loaded, qPrintable(error.message));
        QVERIFY(loaded->warnings.isEmpty());
        QCOMPARE(loaded->identity.size, quint64(1234));
        QCOMPARE(loaded->identity.prefixSha256, QByteArray("deadbeef"));
        QCOMPARE(loaded->set.size(), 2);
        QCOMPARE(loaded->set.annotations()[1], colored);
        QVERIFY(!loaded->set.isDirty());

        // Byte-stable: save(load(save(x))) == save(x).
        QCOMPARE(saveAnnotations(loaded->set, loaded->identity,
                                 QStringLiteral("app.log")), bytes);
    }

    void degradationSkipsBadEntriesOnly()
    {
        const QByteArray json =
            "{ \"version\": 1, \"file\": {\"size\": 1, \"prefixSha256\": \"aa\","
            "  \"prefixLength\": 1 },"
            "  \"annotations\": ["
            "    { \"id\": \"not-a-uuid\", \"startLine\": 1, \"endLine\": 1,"
            "      \"note\": \"x\", \"anchor\": {\"lineSha256\": \"aa\"} },"
            "    { \"id\": \"{11111111-1111-1111-1111-111111111111}\","
            "      \"startLine\": 5, \"endLine\": 3, \"note\": \"x\","
            "      \"anchor\": {\"lineSha256\": \"aa\"} },"
            "    { \"id\": \"{22222222-2222-2222-2222-222222222222}\","
            "      \"startLine\": 2, \"endLine\": 2,"
            "      \"anchor\": {\"lineSha256\": \"aa\"} },"
            "    { \"id\": \"{33333333-3333-3333-3333-333333333333}\","
            "      \"startLine\": 2, \"endLine\": 2, \"note\": \"good\","
            "      \"anchor\": {\"lineSha256\": \"bb\"} } ] }";
        AnnotationFileError error;
        const auto loaded = loadAnnotations(json, &error);
        QVERIFY(loaded);
        QCOMPARE(loaded->set.size(), 1); // only the good one
        QCOMPARE(loaded->set.annotations()[0].note, QStringLiteral("good"));
        QCOMPARE(loaded->warnings.size(), 3);
    }

    void versionAndJsonErrors()
    {
        AnnotationFileError error;
        QVERIFY(!loadAnnotations("{ garbage", &error));
        QVERIFY(error.message.contains("invalid JSON"));
        QVERIFY(!loadAnnotations("[1]", &error));
        QVERIFY(!loadAnnotations("{ \"version\": 2, \"annotations\": [] }", &error));
        QVERIFY(error.message.contains("newer Logdor"));
    }

    void mergeUnionAndLastWriteWins()
    {
        const QDateTime early(QDate(2026, 7, 23), QTime(10, 0), QTimeZone::utc());
        const QDateTime late(QDate(2026, 7, 23), QTime(11, 0), QTimeZone::utc());
        const QUuid shared("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}");

        AnnotationSet mine;
        mine.upsert(makeAnnotation(1, 1, "mine only"));
        mine.upsert(makeAnnotation(10, 10, "old text", early, shared));

        AnnotationSet theirs;
        theirs.upsert(makeAnnotation(2, 2, "theirs only"));
        theirs.upsert(makeAnnotation(10, 10, "new text", late, shared));

        const AnnotationSet merged = mergeAnnotations(mine, theirs);
        QCOMPARE(merged.size(), 3);
        QCOMPARE(merged.find(shared)->note, QStringLiteral("new text"));

        // Merge is order-insensitive for the conflict outcome.
        const AnnotationSet reversed = mergeAnnotations(theirs, mine);
        QCOMPARE(reversed.size(), 3);
        QCOMPARE(reversed.find(shared)->note, QStringLiteral("new text"));

        // Self-merge is idempotent.
        const AnnotationSet self = mergeAnnotations(mine, mine);
        QCOMPARE(self.size(), 2);
        QCOMPARE(self.find(shared)->note, QStringLiteral("old text"));
    }

    void dirtyTracking()
    {
        AnnotationSet set;
        QVERIFY(!set.isDirty());
        set.upsert(makeAnnotation(1, 1, "x"));
        QVERIFY(set.isDirty());
        set.clearDirty();
        set.remove(set.annotations()[0].id);
        QVERIFY(set.isDirty());
    }
};

QTEST_APPLESS_MAIN(tst_Annotation)
#include "tst_annotation.moc"
