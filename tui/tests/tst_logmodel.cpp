#include "LogModel.h"
#include "Session.h"

#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

class tst_LogModel : public QObject {
    Q_OBJECT
private slots:
    void showsAndFiltersLines()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write("alpha one\nbeta two\nalpha three\n");
        file.flush();

        auto source = logdor::FileSource::open(file.fileName());
        QVERIFY(source);
        std::shared_ptr<const logdor::LineIndex> index =
            logdor::buildLineIndex(source).result().index;
        LogModel model(source, index, logdor::parserById(u"plaintext"));
        QCOMPARE(model.rowCount(), 0);
        model.setRows(model.scan({}).result().rows);

        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.columnCount(), 1);
        QCOMPARE(model.data(model.index(1, 0)).toString(), QStringLiteral("beta two"));

        logdor::LineFilter filter;
        filter.query = QStringLiteral("ALPHA");
        const auto result = model.scan(filter).result();
        QCOMPARE(result.matchCount, 2);
        model.setRows(result.rows);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(1, 0)).toString(), QStringLiteral("alpha three"));
    }

    void colorsBySeverity()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write("07-01 00:00:00.067  1651 14988 E Tag: failed\n"
                   "07-01 00:00:00.068  1651 14988 I Tag: started\n");
        file.flush();

        auto source = logdor::FileSource::open(file.fileName());
        QVERIFY(source);
        std::shared_ptr<const logdor::LineIndex> index =
            logdor::buildLineIndex(source).result().index;
        LogModel model(source, index, logdor::parserById(u"logcat"));
        model.setRows(model.scan({}).result().rows);

        QCOMPARE(model.data(model.index(0, 0), Qt::ForegroundRole).toInt(), 0xff6347);
        QCOMPARE(model.data(model.index(1, 0), Qt::ForegroundRole).toInt(), 0x87cefa);
    }

    void sessionKeepsOtherData()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log = dir.filePath(QStringLiteral("a.log"));
        const QString path = dir.filePath(QStringLiteral("sessions.json"));
        QFile out(log);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("one\ntwo\n");
        out.close();
        // A GUI session for this file and one for another file.
        QFile json(path);
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(R"({"version":1,"sessions":{"/other.log":{"filter":{"query":"x"}},)"
                   R"(")" + session::keyFor(log).toUtf8()
                   + R"(":{"pluginStates":{"Log Viewer":{"firstVisibleLine":6}}}}})");
        json.close();

        auto source = logdor::FileSource::open(log);
        QVERIFY(source);
        session::saveFilter(path, *source,
                            {{QStringLiteral("query"), QStringLiteral("Tag=keyv2")},
                             {QStringLiteral("queryMode"), true}});
        const QJsonObject filter = session::loadFilter(path, *source);
        QCOMPARE(filter.value(QStringLiteral("query")).toString(), QStringLiteral("Tag=keyv2"));
        QVERIFY(filter.value(QStringLiteral("queryMode")).toBool());

        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QJsonObject sessions =
            QJsonDocument::fromJson(in.readAll()).object().value(QStringLiteral("sessions")).toObject();
        QCOMPARE(sessions.value(QStringLiteral("/other.log")).toObject()
                     .value(QStringLiteral("filter")).toObject()
                     .value(QStringLiteral("query")).toString(),
                 QStringLiteral("x"));
        QVERIFY(sessions.value(session::keyFor(log)).toObject()
                    .value(QStringLiteral("pluginStates")).toObject()
                    .contains(QStringLiteral("Log Viewer")));

        // Other content in the same file: the saved filter does not apply.
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("replaced\n");
        out.close();
        auto replaced = logdor::FileSource::open(log);
        QVERIFY(session::loadFilter(path, *replaced).isEmpty());
    }
};

QTEST_APPLESS_MAIN(tst_LogModel)
#include "tst_logmodel.moc"
