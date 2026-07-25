// End-to-end test of the Merged Timeline plugin loaded from its real .so:
// two files of different formats (GELF + Docker json-file) go through the
// full pipeline - index, detect, extract, merge - and the table must show
// them interleaved in time order; the shared filter narrows per file.

#include "../../app/src/plugininterface.h"

#include <logdor/FileSource.h>
#include <logdor/LineIndexer.h>

#include <QAbstractItemModel>
#include <QPluginLoader>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString& path, const QByteArrayList& lines)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (const QByteArray& line : lines)
        file.write(line + "\n");
}

} // namespace

class tst_TimelineViewer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        // GELF at t=0s, 2s, 4s; Docker at t=1s, 3s (2026-01-01T00:00Z).
        m_gelfPath = m_dir.filePath("gelf-a.log");
        writeFile(m_gelfPath,
                  { R"({"version":"1.1","host":"web1","short_message":"alpha 0","timestamp":1767225600.0,"level":6})",
                    R"({"version":"1.1","host":"web1","short_message":"alpha 2","timestamp":1767225602.0,"level":4})",
                    R"({"version":"1.1","host":"web1","short_message":"alpha 4 error","timestamp":1767225604.0,"level":3})" });
        m_dockerPath = m_dir.filePath("docker-b.log");
        writeFile(m_dockerPath,
                  { R"({"log":"bravo 1\n","stream":"stdout","time":"2026-01-01T00:00:01.000000000Z"})",
                    R"({"log":"bravo 3 error\n","stream":"stderr","time":"2026-01-01T00:00:03.000000000Z"})" });

        QPluginLoader loader(QStringLiteral(TIMELINE_PLUGIN_PATH));
        QVERIFY2(loader.load(), qPrintable(loader.errorString()));
        m_plugin = qobject_cast<PluginInterface*>(loader.instance());
        QVERIFY(m_plugin);
        m_model = m_plugin->widget()->findChild<QTableView*>()->model();
        QVERIFY(m_model);
    }

    void mergesTwoFormatsInTimeOrder()
    {
        addFile(m_gelfPath);
        QTRY_COMPARE_WITH_TIMEOUT(m_model->rowCount(), 3, 10000);
        addFile(m_dockerPath);
        QTRY_COMPARE_WITH_TIMEOUT(m_model->rowCount(), 5, 10000);

        const QStringList files = { "gelf-a.log", "docker-b.log", "gelf-a.log",
                                    "docker-b.log", "gelf-a.log" };
        const QStringList messages = { "alpha 0", "bravo 1", "alpha 2",
                                       "bravo 3", "alpha 4" };
        for (int row = 0; row < 5; ++row) {
            QCOMPARE(cell(row, 0), files[row]);
            QVERIFY2(cell(row, 3).contains(messages[row]),
                     qPrintable(QStringLiteral("row %1: %2")
                                    .arg(row)
                                    .arg(cell(row, 3))));
        }
    }

    void textFilterNarrowsAndClears()
    {
        m_plugin->setFilter(FilterOptions(QStringLiteral("error")));
        QTRY_COMPARE_WITH_TIMEOUT(m_model->rowCount(), 2, 10000);
        QVERIFY(cell(0, 3).contains(QStringLiteral("bravo 3")));
        QVERIFY(cell(1, 3).contains(QStringLiteral("alpha 4")));

        m_plugin->setFilter(FilterOptions());
        QTRY_COMPARE_WITH_TIMEOUT(m_model->rowCount(), 5, 10000);
    }

    void queryModeUnknownFieldUnconstrains()
    {
        // GELF has a host field, Docker does not: the term filters GELF to
        // nothing but must NOT hide the Docker file (AllowUnknownFields).
        m_plugin->setFilter(FilterOptions(QStringLiteral("host:nope"), 0, 0,
                                          Qt::CaseInsensitive, false,
                                          /*queryMode=*/true));
        QTRY_COMPARE_WITH_TIMEOUT(m_model->rowCount(), 2, 10000);
        QCOMPARE(cell(0, 0), QStringLiteral("docker-b.log"));
        QCOMPARE(cell(1, 0), QStringLiteral("docker-b.log"));

        m_plugin->setFilter(FilterOptions(QStringLiteral("host:web1"), 0, 0,
                                          Qt::CaseInsensitive, false, true));
        QTRY_COMPARE_WITH_TIMEOUT(m_model->rowCount(), 5, 10000);

        m_plugin->setFilter(FilterOptions());
        QTRY_COMPARE_WITH_TIMEOUT(m_model->rowCount(), 5, 10000);
    }

    void newestFirstReversesPresentation()
    {
        auto* toggle = m_plugin->widget()->findChild<QPushButton*>(
            QStringLiteral("timelineNewestFirstButton"));
        QVERIFY(toggle);
        QCOMPARE(toggle->text(), QStringLiteral("Newest First"));
        toggle->click();
        QCOMPARE(m_model->rowCount(), 5);
        QVERIFY(cell(0, 3).contains(QStringLiteral("alpha 4")));
        QVERIFY(cell(4, 3).contains(QStringLiteral("alpha 0")));
        // The label names the action the next click will perform.
        QCOMPARE(toggle->text(), QStringLiteral("Oldest First"));
        toggle->click();
        QVERIFY(cell(0, 3).contains(QStringLiteral("alpha 0")));
        QCOMPARE(toggle->text(), QStringLiteral("Newest First"));
    }

    void addFileToTimelineEventAddsFile()
    {
        // The folder-search route: the shell broadcasts AddFileToTimeline
        // with a path payload.
        const QString path = m_dir.filePath("gelf-c.log");
        writeFile(path,
                  { R"({"version":"1.1","host":"web2","short_message":"charlie","timestamp":1767225601.5,"level":6})" });
        m_plugin->onPluginEvent(PluginEvent::AddFileToTimeline, path);
        QTRY_COMPARE_WITH_TIMEOUT(m_model->rowCount(), 6, 10000);
        QVERIFY(cell(2, 3).contains(QStringLiteral("charlie"))); // t=1.5s
    }

private:
    /// The shell path into the plugin: make @p path the current file and
    /// press "Add Current File".
    void addFile(const QString& path)
    {
        auto source = logdor::FileSource::open(path);
        QVERIFY(source);
        auto future = logdor::buildLineIndex(source);
        future.waitForFinished();
        m_plugin->setCoreSource(source, future.result().index);
        QVERIFY(QMetaObject::invokeMethod(m_plugin, "addCurrentFile"));
    }

    QString cell(int row, int column) const
    {
        return m_model->index(row, column).data().toString();
    }

    QTemporaryDir m_dir;
    QString m_gelfPath;
    QString m_dockerPath;
    PluginInterface* m_plugin = nullptr;
    QAbstractItemModel* m_model = nullptr;
};

QTEST_MAIN(tst_TimelineViewer)
#include "tst_timelineviewer.moc"
