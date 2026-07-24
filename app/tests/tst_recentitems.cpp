#include "../src/recentitems.h"

#include <QTemporaryDir>
#include <QTest>

class tst_RecentItems : public QObject {
    Q_OBJECT

private slots:
    void prependsMostRecentFirst()
    {
        QTemporaryDir dir;
        const QString a = touch(dir, "a.log");
        const QString b = touch(dir, "b.log");

        QStringList items = updatedRecents({}, a);
        items = updatedRecents(items, b);
        QCOMPARE(items, QStringList({ b, a }));
    }

    void reopeningMovesToFront()
    {
        QTemporaryDir dir;
        const QString a = touch(dir, "a.log");
        const QString b = touch(dir, "b.log");

        QStringList items = updatedRecents(updatedRecents({}, a), b);
        items = updatedRecents(items, a);
        QCOMPARE(items, QStringList({ a, b }));
        QCOMPARE(items.size(), 2); // no duplicate of a
    }

    void canonicalizesDotDotSpellings()
    {
        QTemporaryDir dir;
        const QString a = touch(dir, "a.log");
        const QString roundabout =
            dir.path() + QStringLiteral("/./sub/../a.log");
        QDir(dir.path()).mkdir(QStringLiteral("sub"));

        QStringList items = updatedRecents({}, a);
        items = updatedRecents(items, roundabout);
        QCOMPARE(items, QStringList({ a }));
    }

    void capsTheList()
    {
        QTemporaryDir dir;
        QStringList items;
        for (int i = 0; i < 15; ++i)
            items = updatedRecents(items, touch(dir, QString("f%1.log").arg(i)), 10);
        QCOMPARE(items.size(), 10);
        QVERIFY(items.first().endsWith(QStringLiteral("f14.log")));
        QVERIFY(items.last().endsWith(QStringLiteral("f5.log")));
    }

    void keepsFoldersAndFiles()
    {
        QTemporaryDir dir;
        const QString file = touch(dir, "a.log");
        QStringList items = updatedRecents({}, file);
        items = updatedRecents(items, dir.path());
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.first(), QFileInfo(dir.path()).canonicalFilePath());
    }

    void pruneDropsMissingKeepsOrder()
    {
        QTemporaryDir dir;
        const QString a = touch(dir, "a.log");
        const QString b = touch(dir, "b.log");
        const QString c = touch(dir, "c.log");

        QStringList items = { c, b, a };
        QVERIFY(QFile::remove(b));
        QCOMPARE(prunedRecents(items), QStringList({ c, a }));
    }

private:
    static QString touch(const QTemporaryDir& dir, const QString& name)
    {
        const QString path = dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write("x\n");
        f.close();
        return QFileInfo(path).canonicalFilePath();
    }
};

QTEST_MAIN(tst_RecentItems)
#include "tst_recentitems.moc"
