#include <logdor/FileIdentity.h>
#include <logdor/FileSource.h>

#include <QTemporaryDir>
#include <QTest>

using namespace logdor;

namespace {

std::shared_ptr<FileSource> openContent(const QTemporaryDir& dir,
                                        const QString& name,
                                        const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size())
        return nullptr;
    f.close();
    return FileSource::open(path);
}

} // namespace

class tst_FileIdentity : public QObject {
    Q_OBJECT

private slots:
    void sameContentDifferentNameMatches()
    {
        QTemporaryDir dir;
        const QByteArray content(200000, 'x'); // > 64 KiB
        auto a = openContent(dir, "a.log", content);
        auto b = openContent(dir, "renamed.log", content);
        const FileIdentity identity = computeFileIdentity(*a);
        QCOMPARE(identity.prefixLength, quint32(64 * 1024));
        QCOMPARE(matchIdentity(identity, *b), IdentityMatch::Identical);
    }

    void appendedFileIsGrown()
    {
        QTemporaryDir dir;
        const QByteArray content(100000, 'x');
        auto original = openContent(dir, "a.log", content);
        const FileIdentity identity = computeFileIdentity(*original);
        auto grown = openContent(dir, "b.log", content + "more lines appended\n");
        QCOMPARE(matchIdentity(identity, *grown), IdentityMatch::Grown);
    }

    void smallFileGrowingPastPrefixCapStillMatches()
    {
        QTemporaryDir dir;
        const QByteArray small = "just a few lines\n";
        auto original = openContent(dir, "s.log", small);
        const FileIdentity identity = computeFileIdentity(*original);
        QCOMPARE(identity.prefixLength, quint32(small.size()));

        auto grown = openContent(dir, "g.log",
                                 small + QByteArray(200000, 'y'));
        QCOMPARE(matchIdentity(identity, *grown), IdentityMatch::Grown);
    }

    void changedOrTruncatedMismatch()
    {
        QTemporaryDir dir;
        const QByteArray content(100000, 'x');
        auto original = openContent(dir, "a.log", content);
        const FileIdentity identity = computeFileIdentity(*original);

        QByteArray changed = content;
        changed[10] = 'y';
        auto changedSource = openContent(dir, "c.log", changed);
        QCOMPARE(matchIdentity(identity, *changedSource), IdentityMatch::Mismatch);

        auto truncated = openContent(dir, "t.log", content.left(50000));
        QCOMPARE(matchIdentity(identity, *truncated), IdentityMatch::Mismatch);

        QCOMPARE(matchIdentity(FileIdentity {}, *original), IdentityMatch::Mismatch);
    }
};

QTEST_APPLESS_MAIN(tst_FileIdentity)
#include "tst_fileidentity.moc"
