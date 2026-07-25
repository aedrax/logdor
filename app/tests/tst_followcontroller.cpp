// FollowController: growth ticks must deliver an extended index with the
// right firstNewLine; rotation/truncation must demand a full reload.

#include "../src/followcontroller.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace logdor;

Q_DECLARE_METATYPE(std::shared_ptr<logdor::FileSource>)
Q_DECLARE_METATYPE(std::shared_ptr<const logdor::LineIndex>)

namespace {

struct Opened {
    QString path;
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
    return { path, source, future.result().index };
}

bool appendToFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    return f.write(bytes) == bytes.size();
}

// Drive the poll deterministically instead of waiting out the 1 s timer.
void forceTick(FollowController& controller)
{
    QMetaObject::invokeMethod(&controller, "tick");
}

} // namespace

class tst_FollowController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<std::shared_ptr<FileSource>>();
        qRegisterMetaType<std::shared_ptr<const LineIndex>>();
    }

    void growthEmitsExtendedIndex()
    {
        QTemporaryDir dir;
        const Opened o = openContent(dir, "grow.log", "one\ntwo\n");
        QVERIFY(o.source);

        FollowController controller;
        QSignalSpy extended(&controller, &FollowController::extended);
        QSignalSpy rotated(&controller, &FollowController::rotated);
        controller.start(o.path, o.source, o.index);

        forceTick(controller); // no change: idle
        QCOMPARE(extended.count(), 0);

        QVERIFY(appendToFile(o.path, "three\nfour\n"));
        forceTick(controller);
        QTRY_COMPARE_WITH_TIMEOUT(extended.count(), 1, 5000);
        const auto index = extended[0][1]
                               .value<std::shared_ptr<const LineIndex>>();
        QVERIFY(index);
        QCOMPARE(index->lineCount(), qint64(4));
        QCOMPARE(extended[0][2].toLongLong(), qint64(2)); // old count
        QCOMPARE(rotated.count(), 0);

        // The next growth extends from the previously extended index.
        QVERIFY(appendToFile(o.path, "five\n"));
        forceTick(controller);
        QTRY_COMPARE_WITH_TIMEOUT(extended.count(), 2, 5000);
        QCOMPARE(extended[1][1]
                     .value<std::shared_ptr<const LineIndex>>()
                     ->lineCount(),
                 qint64(5));
        QCOMPARE(extended[1][2].toLongLong(), qint64(4));
    }

    void unterminatedTailInvalidatesItsLine()
    {
        QTemporaryDir dir;
        const Opened o = openContent(dir, "tail.log", "one\ntw");
        QVERIFY(o.source);

        FollowController controller;
        QSignalSpy extended(&controller, &FollowController::extended);
        controller.start(o.path, o.source, o.index);

        QVERIFY(appendToFile(o.path, "o\nthree\n"));
        forceTick(controller);
        QTRY_COMPARE_WITH_TIMEOUT(extended.count(), 1, 5000);
        // Line 1 ("tw") grew into "two": consumers must re-evaluate it.
        QCOMPARE(extended[0][2].toLongLong(), qint64(1));
        const auto index = extended[0][1]
                               .value<std::shared_ptr<const LineIndex>>();
        QCOMPARE(index->lineCount(), qint64(3));
        QCOMPARE(index->lengthOf(1), qsizetype(3)); // "two"
    }

    void rotationEmitsRotatedAndStops()
    {
        QTemporaryDir dir;
        const Opened o = openContent(dir, "rot.log", "aaaa\nbbbb\ncccc\n");
        QVERIFY(o.source);

        FollowController controller;
        QSignalSpy extended(&controller, &FollowController::extended);
        QSignalSpy rotated(&controller, &FollowController::rotated);
        controller.start(o.path, o.source, o.index);

        // Rotation: same path, different content (and here, smaller).
        QFile f(o.path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("fresh rotated content\n");
        f.close();

        forceTick(controller);
        QTRY_COMPARE_WITH_TIMEOUT(rotated.count(), 1, 5000);
        QCOMPARE(extended.count(), 0);
        QVERIFY(!controller.isActive());

        forceTick(controller); // stopped: no repeat spam
        QCOMPARE(rotated.count(), 1);
    }

    void stopCancelsQuietly()
    {
        QTemporaryDir dir;
        const Opened o = openContent(dir, "stop.log", "one\n");
        QVERIFY(o.source);

        FollowController controller;
        QSignalSpy extended(&controller, &FollowController::extended);
        controller.start(o.path, o.source, o.index);
        QVERIFY(appendToFile(o.path, "two\n"));
        forceTick(controller);
        controller.stop(); // extend may be in flight; nothing may arrive
        QTest::qWait(200);
        QCOMPARE(extended.count(), 0);
        QVERIFY(!controller.isActive());
    }
};

QTEST_MAIN(tst_FollowController)
#include "tst_followcontroller.moc"
