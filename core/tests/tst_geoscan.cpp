#include <logdor/GeoScan.h>
#include <logdor/LineIndexer.h>

#include <QTemporaryDir>
#include <QTest>

using namespace logdor;

namespace {

// The legacy Map Viewer's sample.log, resurrected as a golden fixture.
QByteArray sampleLog()
{
    return
        "[2025-03-14 10:00:00] System initialized at location: 40.7128\xC2\xB0N, 74.0060\xC2\xB0W (New York City)\n"
        "[2025-03-14 10:15:00] Drone takeoff from coordinates 51.5074, -0.1278 (London)\n"
        "[2025-03-14 10:30:00] Weather station report from 48\xC2\xB0 51' 24\"N, 2\xC2\xB0 21' 03\"E (Paris)\n"
        "[2025-03-14 10:45:00] Satellite position: latitude: 35.6762, longitude: 139.6503 (Tokyo)\n"
        "[2025-03-14 11:00:00] Ground team located at 33\xC2\xB0"
        "51'22\"S, 151\xC2\xB0"
        "12'49\"E (Sydney)\n"
        "[2025-03-14 11:15:00] Emergency signal received from 37.7749\xC2\xB0N, 122.4194\xC2\xB0W (San Francisco)\n"
        "[2025-03-14 11:30:00] Base station coordinates: 52.5200N, 13.4050E (Berlin)\n"
        "[2025-03-14 11:45:00] Monitoring station at lat: -33.9249, long: 18.4241 (Cape Town)\n"
        "[2025-03-14 12:00:00] Research vessel position: 41\xC2\xB0"
        "53'31\"N 12\xC2\xB0"
        "29'07\"E (Rome)\n"
        "[2025-03-14 12:15:00] Field team reporting from 55.7558, 37.6173 (Moscow)\n"
        "[2025-03-14 12:30:00] No coordinates in this log line\n"
        "[2025-03-14 12:45:00] Invalid coordinates: 999.9999, 999.9999\n";
}

bool parse(const char* line, double* lat, double* lon)
{
    return parseCoordinates(QByteArrayView(line, qsizetype(strlen(line))),
                            lat, lon);
}

} // namespace

class tst_GeoScan : public QObject {
    Q_OBJECT

private slots:
    void decimalPatterns_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<double>("lat");
        QTest::addColumn<double>("lon");

        QTest::newRow("plain-pair") << "position 51.5074, -0.1278 reached"
                                    << 51.5074 << -0.1278;
        QTest::newRow("hemisphere") << QString::fromUtf8("at 37.7749\xC2\xB0N, 122.4194\xC2\xB0W now")
                                    << 37.7749 << -122.4194;
        QTest::newRow("hemisphere-nosign") << "coords: 52.5200N, 13.4050E"
                                           << 52.52 << 13.405;
        QTest::newRow("labeled") << "lat: -33.9249, long: 18.4241"
                                 << -33.9249 << 18.4241;
        QTest::newRow("labeled-full") << "latitude: 35.6762, longitude: 139.6503"
                                      << 35.6762 << 139.6503;
        QTest::newRow("south-suffix") << "10.5S, 20.25E" << -10.5 << 20.25;
    }

    void decimalPatterns()
    {
        QFETCH(QString, line);
        QFETCH(double, lat);
        QFETCH(double, lon);
        double gotLat = 0, gotLon = 0;
        const QByteArray utf8 = line.toUtf8();
        QVERIFY(parseCoordinates(QByteArrayView(utf8), &gotLat, &gotLon));
        QCOMPARE(gotLat, lat);
        QCOMPARE(gotLon, lon);
    }

    void dmsPattern()
    {
        double lat = 0, lon = 0;
        const QByteArray sydney = QString::fromUtf8(
            "at 33\xC2\xB0"
            "51'22\"S, 151\xC2\xB0"
            "12'49\"E today").toUtf8();
        QVERIFY(parseCoordinates(QByteArrayView(sydney), &lat, &lon));
        QVERIFY(qAbs(lat - (-(33 + 51 / 60.0 + 22 / 3600.0))) < 1e-9);
        QVERIFY(qAbs(lon - (151 + 12 / 60.0 + 49 / 3600.0)) < 1e-9);
    }

    void rejectsInvalid()
    {
        double lat = 0, lon = 0;
        QVERIFY(!parse("Invalid coordinates: 999.9999, 999.9999", &lat, &lon));
        QVERIFY(!parse("out of range 91.0, 200.0", &lat, &lon));
        QVERIFY(!parse("No coordinates in this log line", &lat, &lon));
        QVERIFY(!parse("", &lat, &lon));
        // A leading timestamp's digits must not read as coordinates.
        QVERIFY(!parse("[2025-03-14 10:00:00] all systems nominal", &lat, &lon));
    }

    void legacyFalsePositiveBehaviorIsPreserved()
    {
        // The legacy simple pattern accepts any in-range "number, number"
        // pair — e.g. versions. Same tradeoff as the old plugin; the hit
        // table in the UI keeps it visible. Pin it so a change is deliberate.
        double lat = 0, lon = 0;
        QVERIFY(parse("upgraded from version 1.2, 3.4 build", &lat, &lon));
        QCOMPARE(lat, 1.2);
        QCOMPARE(lon, 3.4);
    }

    void scanFindsSampleLogPoints()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("sample.log");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(sampleLog());
        f.close();

        auto source = FileSource::open(path);
        auto indexFuture = buildLineIndex(source);
        indexFuture.waitForFinished();
        const auto index = indexFuture.result().index;

        auto future = scanCoordinates(source, index, 3); // tiny chunks
        future.waitForFinished();
        const GeoScanResult result = future.result();

        // 10 coordinate lines; the "No coordinates" and "Invalid" lines miss.
        QCOMPARE(result.points.size(), size_t(10));
        QVERIFY(std::is_sorted(result.points.begin(), result.points.end(),
                               [](const GeoPoint& a, const GeoPoint& b) {
                                   return a.line < b.line;
                               }));
        QCOMPARE(result.points[0].line, 0);
        QCOMPARE(result.points[0].latitude, 40.7128);
        QCOMPARE(result.points[0].longitude, -74.0060);
        QCOMPARE(result.points[1].line, 1); // London
        QCOMPARE(result.points[9].line, 9); // Moscow
    }

    void cancellation()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("big.log");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 0; i < 200'000; ++i)
            f.write("line without any digits here\n");
        f.close();

        auto source = FileSource::open(path);
        auto indexFuture = buildLineIndex(source);
        indexFuture.waitForFinished();

        auto future = scanCoordinates(source, indexFuture.result().index, 64);
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
    }
};

QTEST_APPLESS_MAIN(tst_GeoScan)
#include "tst_geoscan.moc"
