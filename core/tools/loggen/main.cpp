// loggen: deterministic synthetic log generator for tests and benchmarks.
//
// Formats mirror logs the bundled plugins parse (plain text, Android logcat,
// Apache common/combined) so benchmark corpora stay reusable across phases.
// Same seed + flags => byte-identical output.

#include <QByteArray>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <cstdio>
#include <random>

namespace {

constexpr qsizetype kFlushThreshold = 4 * 1024 * 1024;

const char* const kWords[] = {
    "connection", "established", "timeout", "retry", "request", "response",
    "queue", "worker", "thread", "session", "cache", "miss", "hit", "flush",
    "database", "transaction", "commit", "rollback", "index", "shard",
    "packet", "dropped", "received", "buffer", "overflow", "allocated",
    "released", "handler", "dispatch", "event", "signal", "listener",
    "startup", "shutdown", "config", "loaded", "parsed", "invalid", "warning",
    "failure", "recovered", "heartbeat", "latency", "throughput", "backlog",
};
constexpr int kWordCount = int(sizeof(kWords) / sizeof(kWords[0]));

const char* const kLevelsLogcat = "VDIWEF";
const char* const kTags[] = { "ActivityManager", "WifiService", "AudioFlinger",
                              "SurfaceFlinger", "PackageManager", "Zygote" };
const char* const kPaths[] = { "/index.html", "/api/v1/items", "/login",
                               "/static/app.js", "/images/logo.png", "/health" };
const char* const kMethods[] = { "GET", "POST", "PUT", "DELETE" };

qint64 parseBytes(const QString& s, bool* ok)
{
    QString t = s.trimmed().toUpper();
    qint64 mult = 1;
    if (t.endsWith(u'K')) { mult = 1024LL; t.chop(1); }
    else if (t.endsWith(u'M')) { mult = 1024LL * 1024; t.chop(1); }
    else if (t.endsWith(u'G')) { mult = 1024LL * 1024 * 1024; t.chop(1); }
    qint64 n = t.toLongLong(ok);
    return *ok ? n * mult : 0;
}

class Generator {
public:
    Generator(quint64 seed) : m_rng(seed) {}

    int rint(int lo, int hi) // inclusive
    {
        return int(std::uniform_int_distribution<int>(lo, hi)(m_rng));
    }

    // Deterministic clock: fixed start, advances 1..500 ms per line.
    void advanceClock() { m_millis += rint(1, 500); }

    QByteArray timestampLogcat() const
    {
        // MM-DD HH:MM:SS.mmm from a fake epoch starting 07-01 00:00:00.000
        qint64 total = m_millis;
        int ms = total % 1000; total /= 1000;
        int sec = total % 60; total /= 60;
        int min = total % 60; total /= 60;
        int hour = total % 24; total /= 24;
        int day = 1 + int(total % 28);
        char buf[32];
        std::snprintf(buf, sizeof buf, "07-%02d %02d:%02d:%02d.%03d",
                      day, hour, min, sec, ms);
        return buf;
    }

    QByteArray timestampClf() const
    {
        qint64 total = m_millis / 1000;
        int sec = total % 60; total /= 60;
        int min = total % 60; total /= 60;
        int hour = total % 24; total /= 24;
        int day = 1 + int(total % 28);
        char buf[48];
        std::snprintf(buf, sizeof buf, "%02d/Jul/2026:%02d:%02d:%02d -0400",
                      day, hour, min, sec);
        return buf;
    }

    QByteArray sentence(int minWords, int maxWords)
    {
        QByteArray out;
        const int n = rint(minWords, maxWords);
        for (int i = 0; i < n; ++i) {
            if (i) out += ' ';
            out += kWords[rint(0, kWordCount - 1)];
        }
        return out;
    }

    QByteArray plainLine()
    {
        return sentence(3, 24); // ~20-200 bytes
    }

    QByteArray logcatLine()
    {
        QByteArray out = timestampLogcat();
        char buf[64];
        std::snprintf(buf, sizeof buf, " %5d %5d %c ",
                      rint(1000, 32000), rint(1000, 32000),
                      kLevelsLogcat[rint(0, 5)]);
        out += buf;
        out += kTags[rint(0, 5)];
        out += ": ";
        out += sentence(2, 16);
        return out;
    }

    QByteArray clfLine()
    {
        char ip[24];
        std::snprintf(ip, sizeof ip, "192.168.%d.%d", rint(0, 255), rint(1, 254));
        QByteArray out = ip;
        out += " - - [";
        out += timestampClf();
        out += "] \"";
        out += kMethods[rint(0, 3)];
        out += ' ';
        out += kPaths[rint(0, 5)];
        out += " HTTP/1.1\" ";
        static const int codes[] = { 200, 200, 200, 200, 301, 304, 404, 500 };
        out += QByteArray::number(codes[rint(0, 7)]);
        out += ' ';
        out += QByteArray::number(rint(64, 65536));
        out += " \"-\" \"loggen/1.0\"";
        return out;
    }

    QByteArray longLine(qint64 bytes)
    {
        QByteArray out;
        out.reserve(bytes + 16);
        out += "LONGLINE ";
        while (out.size() < bytes)
            out += kWords[rint(0, kWordCount - 1)], out += ' ';
        out.truncate(bytes);
        return out;
    }

private:
    std::mt19937_64 m_rng;
    qint64 m_millis = 0;
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("loggen");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Deterministic synthetic log generator (same seed => identical output)");
    parser.addHelpOption();
    parser.addOptions({
        { "format", "Log format: plain | logcat | clf", "format", "plain" },
        { "bytes", "Target size; suffixes K/M/G allowed (stops at first line "
                   "boundary past the target)", "n", "16M" },
        { "seed", "PRNG seed", "n", "42" },
        { "crlf", "Terminate lines with \\r\\n instead of \\n" },
        { "long-line-every", "Every Nth line is a long line (0 = never)", "n", "0" },
        { "long-line-bytes", "Length of long lines", "n", "65536" },
        { "empty-line-every", "Every Nth line is empty (0 = never)", "n", "0" },
        { "out", "Output file (required)", "file" },
    });
    parser.process(app);

    const QString format = parser.value("format");
    const QString outPath = parser.value("out");
    bool okBytes = false, okSeed = false;
    const qint64 targetBytes = parseBytes(parser.value("bytes"), &okBytes);
    const quint64 seed = parser.value("seed").toULongLong(&okSeed);
    const qint64 longEvery = parser.value("long-line-every").toLongLong();
    const qint64 longBytes = parseBytes(parser.value("long-line-bytes"), &okBytes);
    const qint64 emptyEvery = parser.value("empty-line-every").toLongLong();
    const bool crlf = parser.isSet("crlf");

    if (outPath.isEmpty() || !okBytes || !okSeed || targetBytes <= 0) {
        std::fprintf(stderr, "loggen: --out and a valid --bytes are required\n");
        return 2;
    }
    if (format != "plain" && format != "logcat" && format != "clf") {
        std::fprintf(stderr, "loggen: unknown format '%s'\n", qPrintable(format));
        return 2;
    }

    // Idempotent: generation is deterministic, so an existing file at or past
    // the target size is the file we would produce.
    if (QFileInfo::exists(outPath) && QFileInfo(outPath).size() >= targetBytes) {
        std::printf("loggen: %s already exists (%lld bytes), skipping\n",
                    qPrintable(outPath), (long long)QFileInfo(outPath).size());
        return 0;
    }

    QFileInfo(outPath).dir().mkpath(".");
    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "loggen: cannot open %s: %s\n",
                     qPrintable(outPath), qPrintable(out.errorString()));
        return 1;
    }

    Generator gen(seed);
    const QByteArray eol = crlf ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
    QByteArray buffer;
    buffer.reserve(kFlushThreshold + 2 * 1024 * 1024);

    qint64 written = 0;
    qint64 lineNo = 0;
    while (written + buffer.size() < targetBytes) {
        ++lineNo;
        if (emptyEvery > 0 && lineNo % emptyEvery == 0) {
            // empty line
        } else if (longEvery > 0 && lineNo % longEvery == 0) {
            buffer += gen.longLine(longBytes);
        } else if (format == "logcat") {
            gen.advanceClock();
            buffer += gen.logcatLine();
        } else if (format == "clf") {
            gen.advanceClock();
            buffer += gen.clfLine();
        } else {
            buffer += gen.plainLine();
        }
        buffer += eol;

        if (buffer.size() >= kFlushThreshold) {
            if (out.write(buffer) != buffer.size()) {
                std::fprintf(stderr, "loggen: write failed: %s\n",
                             qPrintable(out.errorString()));
                return 1;
            }
            written += buffer.size();
            buffer.clear();
        }
    }
    if (!buffer.isEmpty()) {
        if (out.write(buffer) != buffer.size()) {
            std::fprintf(stderr, "loggen: write failed: %s\n",
                         qPrintable(out.errorString()));
            return 1;
        }
        written += buffer.size();
    }
    out.close();

    std::printf("loggen: wrote %lld bytes, %lld lines to %s\n",
                (long long)written, (long long)lineNo, qPrintable(outPath));
    return 0;
}
