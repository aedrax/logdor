#include <logdor/DeclarativeParser.h>
#include <logdor/FileSource.h>
#include <logdor/FormatRegistry.h>
#include <logdor/FormatSpec.h>
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
    if (!source)
        return {};
    auto future = buildLineIndex(source);
    future.waitForFinished();
    return { source, future.result().index };
}

QByteArray repeatLines(const QByteArray& line, int count)
{
    QByteArray out;
    for (int i = 0; i < count; ++i) {
        out += line;
        out += " #" + QByteArray::number(i) + "\n";
    }
    return out;
}

} // namespace

class tst_FormatDetect : public QObject {
    Q_OBJECT

private slots:
    void registryRoundTrip()
    {
        QVERIFY(parserById(u"plaintext"));
        QVERIFY(parserById(u"logcat"));
        QVERIFY(parserById(u"clf"));
        QVERIFY(parserById(u"docker-json"));
        QVERIFY(!parserById(u"nope"));
        // Fresh instances per call, identity by id.
        QVERIFY(parserById(u"logcat").get() != parserById(u"logcat").get());
    }

    void pureLogcatWins()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "l.log",
            repeatLines("01-02 03:04:05.678 1234 5678 I ActivityManager: start", 300));
        const auto scores = detectFormat(*o.source, *o.index);
        QCOMPARE(scores.front().parserId, QStringLiteral("logcat"));
    }

    void pureClfWins()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "c.log",
            repeatLines("10.0.0.1 - - [10/Oct/2000:13:55:36 -0700] \"GET / HTTP/1.1\" 200 512", 300));
        const auto scores = detectFormat(*o.source, *o.index);
        QCOMPARE(scores.front().parserId, QStringLiteral("clf"));
    }

    void proseFallsBackToPlaintext()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "p.log",
            repeatLines("the quick brown fox jumps over the lazy dog", 300));
        const auto scores = detectFormat(*o.source, *o.index);
        QCOMPARE(scores.front().parserId, QStringLiteral("plaintext"));
    }

    void logcatSurvivesGarbage()
    {
        // 70% logcat, 30% garbage: 0.7*0.9 = 0.63 > plaintext 0.05.
        QTemporaryDir dir;
        QByteArray mixed;
        for (int i = 0; i < 300; ++i) {
            mixed += (i % 10 < 7)
                ? QByteArray("01-02 03:04:05.678 1 2 D Zygote: fork ") + QByteArray::number(i)
                : QByteArray("random junk line ") + QByteArray::number(i);
            mixed += '\n';
        }
        auto o = openContent(dir, "m.log", mixed);
        const auto scores = detectFormat(*o.source, *o.index);
        QCOMPARE(scores.front().parserId, QStringLiteral("logcat"));
    }

    void emptyFileGetsPlaintextFloor()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "e.log", QByteArray());
        const auto scores = detectFormat(*o.source, *o.index);
        QVERIFY(!scores.isEmpty());
        QCOMPARE(scores.front().parserId, QStringLiteral("plaintext"));
    }

    void declarativeParserWinsDetection()
    {
        // A spec-defined format outranks plaintext on its own sample via the
        // parameterized detectFormat overload.
        const QByteArray specJson =
            "{ \"id\": \"colonsep\", \"displayName\": \"Colon Separated\","
            "  \"pattern\": \"^(?<a>\\\\w+)::(?<msg>.*)$\","
            "  \"fields\": ["
            "    { \"name\": \"Key\", \"capture\": \"a\" },"
            "    { \"name\": \"Message\", \"capture\": \"msg\", \"hint\": \"message\" } ] }";
        FormatSpecError specError;
        auto spec = parseFormatSpec(specJson, "inline", &specError);
        QVERIFY2(spec.has_value(), qPrintable(specError.message));

        auto parsers = builtinParsers();
        parsers.append(std::make_shared<const DeclarativeParser>(std::move(*spec)));

        QTemporaryDir dir;
        auto o = openContent(dir, "d.log", repeatLines("key::some message", 250));
        const auto scores = detectFormat(*o.source, *o.index, parsers);
        QCOMPARE(scores.front().parserId, QStringLiteral("colonsep"));
        QVERIFY(parserById(u"colonsep", parsers));
        QVERIFY(!parserById(u"colonsep")); // not a builtin
    }

    void emptyLinesAreSkippedInSample()
    {
        QTemporaryDir dir;
        // 5 logcat lines drowned in 250 empty lines: still logcat.
        QByteArray content(250, '\n');
        content += repeatLines("01-02 03:04:05.678 1 2 I Tag: msg", 5);
        auto o = openContent(dir, "s.log", content);
        const auto scores = detectFormat(*o.source, *o.index);
        QCOMPARE(scores.front().parserId, QStringLiteral("logcat"));
    }
};

QTEST_APPLESS_MAIN(tst_FormatDetect)
#include "tst_formatdetect.moc"
