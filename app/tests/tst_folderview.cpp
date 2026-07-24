#include "../src/folderview.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

namespace {

bool touch(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write("x\n");
    return true;
}

} // namespace

class tst_FolderView : public QObject {
    Q_OBJECT

    // One fixture: a.log, b.log, c.txt at the root; nested sub/c.log;
    // a sidecar that must stay hidden.
    void populateFixture(const QTemporaryDir& dir)
    {
        QVERIFY(touch(dir.filePath("a.log")));
        QVERIFY(touch(dir.filePath("b.log")));
        QVERIFY(touch(dir.filePath("c.txt")));
        QVERIFY(touch(dir.filePath("a.log.logdor.json")));
        QVERIFY(touch(dir.filePath("sub/c.log")));
    }

private slots:
    void listsRecursivelyAndHidesSidecars()
    {
        QTemporaryDir dir;
        populateFixture(dir);

        FolderView view;
        view.setFolder(dir.path());
        QTRY_COMPARE(view.files().size(), 4); // async population converges

        const QStringList files = view.files();
        // Directories sort first, then names: sub/c.log leads.
        QVERIFY(files.at(0).endsWith(QStringLiteral("sub/c.log")));
        QVERIFY(files.at(1).endsWith(QStringLiteral("/a.log")));
        QVERIFY(files.at(2).endsWith(QStringLiteral("/b.log")));
        QVERIFY(files.at(3).endsWith(QStringLiteral("/c.txt")));
        for (const QString& file : files)
            QVERIFY(!file.endsWith(QStringLiteral(".logdor.json")));
    }

    void nextPreviousCycleWithWrap()
    {
        QTemporaryDir dir;
        populateFixture(dir);

        FolderView view;
        view.setFolder(dir.path());
        QTRY_COMPARE(view.files().size(), 4);
        const QStringList files = view.files();

        QSignalSpy spy(&view, &FolderView::fileActivated);
        for (int i = 0; i < 5; ++i)
            view.selectNext();
        QCOMPARE(spy.count(), 5);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(spy.at(i).first().toString(), files.at(i));
        QCOMPARE(spy.at(4).first().toString(), files.at(0)); // wrapped

        view.selectPrevious();
        QCOMPARE(spy.count(), 6);
        QCOMPARE(spy.last().first().toString(), files.at(3)); // wrapped back
    }

    void setCurrentFileDoesNotEcho()
    {
        QTemporaryDir dir;
        populateFixture(dir);

        FolderView view;
        view.setFolder(dir.path());
        QTRY_COMPARE(view.files().size(), 4);

        QSignalSpy spy(&view, &FolderView::fileActivated);
        view.setCurrentFile(dir.filePath("b.log"));
        QTest::qWait(300); // longer than the selection debounce
        QCOMPARE(spy.count(), 0);

        // Paths outside the folder are ignored, not selected.
        view.setCurrentFile(QDir::tempPath() + QStringLiteral("/elsewhere.log"));
        QCOMPARE(spy.count(), 0);

        // ...and selectNext advances from the highlighted file.
        view.selectNext();
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.last().first().toString().endsWith(QStringLiteral("/c.txt")));
    }

    void selectionFollowDebounceCoalesces()
    {
        QTemporaryDir dir;
        populateFixture(dir);

        FolderView view;
        view.setFolder(dir.path());
        QTRY_COMPARE(view.files().size(), 4);
        view.setCurrentFile(dir.filePath("a.log"));

        // Rapid browsing: two selection moves inside the debounce window
        // produce exactly one activation (the last file).
        QSignalSpy spy(&view, &FolderView::fileActivated);
        auto* tree = view.treeView();
        QTest::keyClick(tree, Qt::Key_Down);
        QTest::keyClick(tree, Qt::Key_Down);
        QTRY_COMPARE(spy.count(), 1);
        QTest::qWait(300);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(tst_FolderView)
#include "tst_folderview.moc"
