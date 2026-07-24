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
        out += '\n';
    }
    return out;
}

} // namespace

// Golden coverage for the system-log format specs shipped in core/formats
// (syslog-rfc3164, syslog-iso, dpkg, dmesg, apt-history, apt-term,
// cloud-init, cloud-init-output, apport, xorg, alternatives), using lines
// captured from real Ubuntu /var/log files, plus detection checks against
// the full parser list.
class tst_SystemFormats : public QObject {
    Q_OBJECT

    QList<std::shared_ptr<const FormatParser>> m_parsers;

private slots:
    void initTestCase()
    {
        const QString dir = QFINDTESTDATA("../formats");
        QVERIFY(!dir.isEmpty());
        QList<FormatSpecError> errors;
        auto specs = loadFormatSpecs({ dir }, &errors);
        for (const auto& e : std::as_const(errors))
            qWarning("%s: %s", qPrintable(e.sourcePath), qPrintable(e.message));
        QVERIFY(errors.isEmpty());

        m_parsers = builtinParsers();
        for (auto& spec : specs)
            m_parsers.append(std::make_shared<const DeclarativeParser>(std::move(spec)));

        const QStringList required{ "syslog-rfc3164", "syslog-iso", "dpkg", "dmesg",
                                    "apt-history", "apt-term", "cloud-init",
                                    "cloud-init-output", "apport", "xorg",
                                    "alternatives" };
        for (const QString& id : required)
            QVERIFY2(parserById(id, m_parsers), qPrintable(id));
    }

    void golden_data()
    {
        QTest::addColumn<QString>("parserId");
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QStringList>("expected");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<int>("severity");

        //=== syslog-iso: Time, Host, Process, PID, Message ==================
        QTest::newRow("iso-systemd-pid")
            << "syslog-iso"
            << QByteArray("2026-07-19T00:00:02.026813-04:00 psorensen2404 systemd[1]: "
                          "rsyslog.service: Sent signal SIGHUP to main process 2713")
            << QStringList{ "2026-07-19T00:00:02.026813-04:00", "psorensen2404",
                            "systemd", "1",
                            "rsyslog.service: Sent signal SIGHUP to main process 2713" }
            << true << int(Severity::None);
        QTest::newRow("iso-no-pid")
            << "syslog-iso"
            << QByteArray("2026-07-19T00:00:02.027015-04:00 psorensen2404 rsyslogd: "
                          "[origin software=\"rsyslogd\"] rsyslogd was HUPed")
            << QStringList{ "2026-07-19T00:00:02.027015-04:00", "psorensen2404",
                            "rsyslogd", "",
                            "[origin software=\"rsyslogd\"] rsyslogd was HUPed" }
            << true << int(Severity::None);
        QTest::newRow("iso-auth-pam")
            << "syslog-iso"
            << QByteArray("2026-07-19T00:05:01.032005-04:00 psorensen2404 CRON[177922]: "
                          "pam_unix(cron:session): session opened for user root(uid=0) by root(uid=0)")
            << QStringList{ "2026-07-19T00:05:01.032005-04:00", "psorensen2404",
                            "CRON", "177922",
                            "pam_unix(cron:session): session opened for user root(uid=0) by root(uid=0)" }
            << true << int(Severity::None);
        QTest::newRow("iso-kernel")
            << "syslog-iso"
            << QByteArray("2026-07-19T02:05:30.090275-04:00 psorensen2404 kernel: "
                          "iwlwifi 0000:00:14.3: missed beacons exceeds threshold")
            << QStringList{ "2026-07-19T02:05:30.090275-04:00", "psorensen2404",
                            "kernel", "",
                            "iwlwifi 0000:00:14.3: missed beacons exceeds threshold" }
            << true << int(Severity::None);
        QTest::newRow("iso-bare-pid-no-process")
            << "syslog-iso"
            << QByteArray("2026-07-23T14:01:29.755271-04:00 psorensen2404 [83950]: "
                          "Failed to spawn the wsdd daemon")
            << QStringList{ "2026-07-23T14:01:29.755271-04:00", "psorensen2404",
                            "", "83950", "Failed to spawn the wsdd daemon" }
            << true << int(Severity::None);
        QTest::newRow("iso-nonmatch-rfc3164-line")
            << "syslog-iso"
            << QByteArray("Jul 24 14:30:01 ubuntu-server CRON[12345]: (root) CMD (true)")
            << QStringList{ "", "", "", "",
                            "Jul 24 14:30:01 ubuntu-server CRON[12345]: (root) CMD (true)" }
            << false << int(Severity::None);

        //=== syslog-rfc3164: Time, Host, Process, PID, Message ==============
        QTest::newRow("rfc3164-cron")
            << "syslog-rfc3164"
            << QByteArray("Jul 24 14:30:01 ubuntu-server CRON[12345]: (root) CMD "
                          "(command -v debian-sa1 > /dev/null && debian-sa1 1 1)")
            << QStringList{ "Jul 24 14:30:01", "ubuntu-server", "CRON", "12345",
                            "(root) CMD (command -v debian-sa1 > /dev/null && debian-sa1 1 1)" }
            << true << int(Severity::None);
        QTest::newRow("rfc3164-single-digit-day")
            << "syslog-rfc3164"
            << QByteArray("Jul  1 03:04:05 myhost rsyslogd: rsyslogd was HUPed")
            << QStringList{ "Jul  1 03:04:05", "myhost", "rsyslogd", "",
                            "rsyslogd was HUPed" }
            << true << int(Severity::None);

        //=== dpkg: Time, Action, Details ====================================
        QTest::newRow("dpkg-startup")
            << "dpkg"
            << QByteArray("2026-07-06 12:19:48 startup archives unpack")
            << QStringList{ "2026-07-06 12:19:48", "startup", "archives unpack" }
            << true << int(Severity::None);
        QTest::newRow("dpkg-upgrade")
            << "dpkg"
            << QByteArray("2026-07-06 12:19:50 upgrade printer-driver-postscript-hp:amd64 "
                          "3.23.12+dfsg0-0ubuntu5 3.23.12+dfsg0-0ubuntu5.1")
            << QStringList{ "2026-07-06 12:19:50", "upgrade",
                            "printer-driver-postscript-hp:amd64 "
                            "3.23.12+dfsg0-0ubuntu5 3.23.12+dfsg0-0ubuntu5.1" }
            << true << int(Severity::None);
        QTest::newRow("dpkg-status")
            << "dpkg"
            << QByteArray("2026-07-06 12:19:50 status half-configured "
                          "printer-driver-postscript-hp:amd64 3.23.12+dfsg0-0ubuntu5")
            << QStringList{ "2026-07-06 12:19:50", "status",
                            "half-configured printer-driver-postscript-hp:amd64 "
                            "3.23.12+dfsg0-0ubuntu5" }
            << true << int(Severity::None);
        QTest::newRow("dpkg-install-none")
            << "dpkg"
            << QByteArray("2026-07-24 10:15:30 install curl:amd64 <none> 7.81.0-1ubuntu1.16")
            << QStringList{ "2026-07-24 10:15:30", "install",
                            "curl:amd64 <none> 7.81.0-1ubuntu1.16" }
            << true << int(Severity::None);
        QTest::newRow("dpkg-nonmatch")
            << "dpkg"
            << QByteArray("not a dpkg line")
            << QStringList{ "", "", "not a dpkg line" }
            << false << int(Severity::None);

        //=== dmesg: Time, Message ===========================================
        QTest::newRow("dmesg-file-kernel-prefix")
            << "dmesg"
            << QByteArray("[    0.000000] kernel: Linux version 7.0.0-28-generic "
                          "(buildd@lcy02-amd64-004)")
            << QStringList{ "0.000000",
                            "kernel: Linux version 7.0.0-28-generic (buildd@lcy02-amd64-004)" }
            << true << int(Severity::None);
        QTest::newRow("dmesg-raw-colon-heavy")
            << "dmesg"
            << QByteArray("[    1.234567] e1000e 0000:00:1f.6 eth0: "
                          "(PCI Express:2.5GT/s:Width x1) 00:1a:2b:3c:4d:5e")
            << QStringList{ "1.234567",
                            "e1000e 0000:00:1f.6 eth0: (PCI Express:2.5GT/s:Width x1) "
                            "00:1a:2b:3c:4d:5e" }
            << true << int(Severity::None);
        QTest::newRow("dmesg-nonmatch")
            << "dmesg"
            << QByteArray("Booting the kernel.")
            << QStringList{ "", "Booting the kernel." }
            << false << int(Severity::None);

        //=== apt-history: Key, Value (per-line view of the block format) ====
        QTest::newRow("apt-history-start-date")
            << "apt-history"
            << QByteArray("Start-Date: 2026-07-06  12:19:48")
            << QStringList{ "Start-Date", "2026-07-06  12:19:48" }
            << true << int(Severity::None);
        QTest::newRow("apt-history-commandline")
            << "apt-history"
            << QByteArray("Commandline: /usr/bin/unattended-upgrade")
            << QStringList{ "Commandline", "/usr/bin/unattended-upgrade" }
            << true << int(Severity::None);
        QTest::newRow("apt-history-requested-by")
            << "apt-history"
            << QByteArray("Requested-By: psorensen (1000)")
            << QStringList{ "Requested-By", "psorensen (1000)" }
            << true << int(Severity::None);
        QTest::newRow("apt-history-upgrade")
            << "apt-history"
            << QByteArray("Upgrade: socat:amd64 (1.8.0.0-4build3, 1.8.0.0-4ubuntu0.1)")
            << QStringList{ "Upgrade",
                            "socat:amd64 (1.8.0.0-4build3, 1.8.0.0-4ubuntu0.1)" }
            << true << int(Severity::None);
        QTest::newRow("apt-history-error")
            << "apt-history"
            << QByteArray("Error: Sub-process /usr/bin/dpkg returned an error code (1)")
            << QStringList{ "Error",
                            "Sub-process /usr/bin/dpkg returned an error code (1)" }
            << true << int(Severity::Error);
        QTest::newRow("apt-history-nonmatch")
            << "apt-history"
            << QByteArray("Not-A-Key: something")
            << QStringList{ "", "Not-A-Key: something" }
            << false << int(Severity::None);

        //=== apt-term: Action, Details (best-effort dpkg action lines) ======
        QTest::newRow("apt-term-log-started")
            << "apt-term"
            << QByteArray("Log started: 2026-07-06  12:19:48")
            << QStringList{ "Log started", "2026-07-06  12:19:48" }
            << true << int(Severity::None);
        QTest::newRow("apt-term-preparing")
            << "apt-term"
            << QByteArray("Preparing to unpack "
                          ".../0-printer-driver-postscript-hp_3.23.12+dfsg0-0ubuntu5.1_amd64.deb ...")
            << QStringList{ "Preparing to unpack",
                            ".../0-printer-driver-postscript-hp_3.23.12+dfsg0-0ubuntu5.1_amd64.deb ..." }
            << true << int(Severity::None);
        QTest::newRow("apt-term-unpacking")
            << "apt-term"
            << QByteArray("Unpacking printer-driver-postscript-hp (3.23.12+dfsg0-0ubuntu5.1) "
                          "over (3.23.12+dfsg0-0ubuntu5) ...")
            << QStringList{ "Unpacking",
                            "printer-driver-postscript-hp (3.23.12+dfsg0-0ubuntu5.1) "
                            "over (3.23.12+dfsg0-0ubuntu5) ..." }
            << true << int(Severity::None);
        QTest::newRow("apt-term-triggers")
            << "apt-term"
            << QByteArray("Processing triggers for libc-bin (2.39-0ubuntu8.7) ...")
            << QStringList{ "Processing triggers for",
                            "libc-bin (2.39-0ubuntu8.7) ..." }
            << true << int(Severity::None);
        QTest::newRow("apt-term-nonmatch-progress")
            << "apt-term"
            << QByteArray("(Reading database ... 5%")
            << QStringList{ "", "(Reading database ... 5%" }
            << false << int(Severity::None);

        //=== cloud-init: Time, Module, Level, Message =======================
        QTest::newRow("cloud-init-info")
            << "cloud-init"
            << QByteArray("2025-04-02 15:33:09,851 - main.py[INFO]: "
                          "PID [1] started cloud-init 'init-local'.")
            << QStringList{ "2025-04-02 15:33:09,851", "main.py", "INFO",
                            "PID [1] started cloud-init 'init-local'." }
            << true << int(Severity::Info);
        QTest::newRow("cloud-init-debug")
            << "cloud-init"
            << QByteArray("2025-04-02 15:33:09,851 - main.py[DEBUG]: "
                          "No kernel command line url found.")
            << QStringList{ "2025-04-02 15:33:09,851", "main.py", "DEBUG",
                            "No kernel command line url found." }
            << true << int(Severity::Debug);
        QTest::newRow("cloud-init-warning")
            << "cloud-init"
            << QByteArray("2025-04-02 15:33:11,815 - networking.py[WARNING]: "
                          "Not all expected physical devices present: {'cc:96:e5:ce:f0:04'}")
            << QStringList{ "2025-04-02 15:33:11,815", "networking.py", "WARNING",
                            "Not all expected physical devices present: {'cc:96:e5:ce:f0:04'}" }
            << true << int(Severity::Warning);
        QTest::newRow("cloud-init-error")
            << "cloud-init"
            << QByteArray("2025-04-02 15:33:11,815 - main.py[ERROR]: failed stage init-local")
            << QStringList{ "2025-04-02 15:33:11,815", "main.py", "ERROR",
                            "failed stage init-local" }
            << true << int(Severity::Error);
        QTest::newRow("cloud-init-nonmatch-traceback")
            << "cloud-init"
            << QByteArray("Traceback (most recent call last):")
            << QStringList{ "", "", "", "Traceback (most recent call last):" }
            << false << int(Severity::None);

        //=== cloud-init-output: Time, Module, Level, Message ================
        QTest::newRow("cloud-init-output-logging-line")
            << "cloud-init-output"
            << QByteArray("2025-04-02 15:33:11,815 - main.py[ERROR]: failed stage init-local")
            << QStringList{ "2025-04-02 15:33:11,815", "main.py", "ERROR",
                            "failed stage init-local" }
            << true << int(Severity::Error);
        QTest::newRow("cloud-init-output-banner")
            << "cloud-init-output"
            << QByteArray("Cloud-init v. 24.4.1-0ubuntu0~24.04.2 running 'init-local' "
                          "at Wed, 02 Apr 2025 15:33:09 +0000. Up 14.55 seconds.")
            << QStringList{ "", "", "",
                            "Cloud-init v. 24.4.1-0ubuntu0~24.04.2 running 'init-local' "
                            "at Wed, 02 Apr 2025 15:33:09 +0000. Up 14.55 seconds." }
            << true << int(Severity::None);
        QTest::newRow("cloud-init-output-nonmatch-stdout")
            << "cloud-init-output"
            << QByteArray("cloudinit.sources.DataSourceNotFoundException: "
                          "Did not find any data source, searched classes: ()")
            << QStringList{ "", "", "",
                            "cloudinit.sources.DataSourceNotFoundException: "
                            "Did not find any data source, searched classes: ()" }
            << false << int(Severity::None);

        //=== apport: Level, PID, Time, Message ==============================
        QTest::newRow("apport-info")
            << "apport"
            << QByteArray("INFO: apport (pid 1090270) 2026-07-24 11:58:36,091: "
                          "called for global pid 640427, signal 6, core limit 0, dump mode 1")
            << QStringList{ "INFO", "1090270", "2026-07-24 11:58:36,091",
                            "called for global pid 640427, signal 6, core limit 0, dump mode 1" }
            << true << int(Severity::Info);
        QTest::newRow("apport-error")
            << "apport"
            << QByteArray("ERROR: apport (pid 1090270) 2026-07-24 11:58:36,093: "
                          "report /var/crash/_usr_sbin_cupsd.0.crash already exists and unseen, "
                          "skipping to avoid disk usage DoS")
            << QStringList{ "ERROR", "1090270", "2026-07-24 11:58:36,093",
                            "report /var/crash/_usr_sbin_cupsd.0.crash already exists and unseen, "
                            "skipping to avoid disk usage DoS" }
            << true << int(Severity::Error);
        QTest::newRow("apport-nonmatch")
            << "apport"
            << QByteArray("called for pid 5678, signal 11, core limit 0")
            << QStringList{ "", "", "", "called for pid 5678, signal 11, core limit 0" }
            << false << int(Severity::None);

        //=== xorg: Time, Marker, Message ====================================
        QTest::newRow("xorg-probed-marker")
            << "xorg"
            << QByteArray("[    26.235] (--) Log file renamed from "
                          "\"/var/log/Xorg.pid-3168.log\" to \"/var/log/Xorg.0.log\"")
            << QStringList{ "26.235", "--",
                            "Log file renamed from \"/var/log/Xorg.pid-3168.log\" "
                            "to \"/var/log/Xorg.0.log\"" }
            << true << int(Severity::None);
        QTest::newRow("xorg-info")
            << "xorg"
            << QByteArray("[    26.240] (II) The server relies on udev to provide "
                          "the list of input devices.")
            << QStringList{ "26.240", "II",
                            "The server relies on udev to provide the list of input devices." }
            << true << int(Severity::Info);
        QTest::newRow("xorg-warning")
            << "xorg"
            << QByteArray("[    26.240] (WW) The directory "
                          "\"/usr/share/fonts/X11/cyrillic\" does not exist.")
            << QStringList{ "26.240", "WW",
                            "The directory \"/usr/share/fonts/X11/cyrillic\" does not exist." }
            << true << int(Severity::Warning);
        QTest::newRow("xorg-error")
            << "xorg"
            << QByteArray("[ 25893.227] (EE) event4  - VEN_06CB:00 06CB:CE7E Touchpad: "
                          "kernel bug: Touch jump detected and discarded.")
            << QStringList{ "25893.227", "EE",
                            "event4  - VEN_06CB:00 06CB:CE7E Touchpad: "
                            "kernel bug: Touch jump detected and discarded." }
            << true << int(Severity::Error);
        QTest::newRow("xorg-no-marker")
            << "xorg"
            << QByteArray("[    26.236] Current Operating System: "
                          "Linux psorensen2404 7.0.0-28-generic")
            << QStringList{ "26.236", "",
                            "Current Operating System: Linux psorensen2404 7.0.0-28-generic" }
            << true << int(Severity::None);
        QTest::newRow("xorg-nonmatch-header")
            << "xorg"
            << QByteArray("X.Org X Server 1.21.1.11")
            << QStringList{ "", "", "X.Org X Server 1.21.1.11" }
            << false << int(Severity::None);
        // Kernel timestamps carry 6 fractional digits, Xorg exactly 3 - the
        // differentiator that keeps dmesg files away from the xorg spec.
        QTest::newRow("xorg-nonmatch-dmesg-line")
            << "xorg"
            << QByteArray("[    0.000000] kernel: Linux version 7.0.0-28-generic "
                          "(buildd@lcy02-amd64-004)")
            << QStringList{ "", "",
                            "[    0.000000] kernel: Linux version 7.0.0-28-generic "
                            "(buildd@lcy02-amd64-004)" }
            << false << int(Severity::None);

        //=== alternatives: Time, Message ====================================
        QTest::newRow("alternatives-install")
            << "alternatives"
            << QByteArray("update-alternatives 2026-07-07 06:43:30: run with "
                          "--install /usr/bin/rview rview /usr/bin/vim.basic 30")
            << QStringList{ "2026-07-07 06:43:30",
                            "run with --install /usr/bin/rview rview /usr/bin/vim.basic 30" }
            << true << int(Severity::None);
        QTest::newRow("alternatives-nonmatch")
            << "alternatives"
            << QByteArray("update-alternatives: warning: forcing reinstallation")
            << QStringList{ "", "update-alternatives: warning: forcing reinstallation" }
            << false << int(Severity::None);
    }

    void golden()
    {
        QFETCH(QString, parserId);
        QFETCH(QByteArray, line);
        QFETCH(QStringList, expected);
        QFETCH(bool, ok);
        QFETCH(int, severity);

        const auto parser = parserById(parserId, m_parsers);
        QVERIFY(parser);
        QCOMPARE(parser->schema().size(), expected.size());

        ParsedRow row;
        parser->parseLine(QByteArrayView(line), row);
        QCOMPARE(row.ok, ok);
        QCOMPARE(row.fields.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(row.fields[i], expected[i]);
        QCOMPARE(int(row.severity), severity);
        QCOMPARE(parser->matchesStructure(QByteArrayView(line)), ok);
    }

    void detection_data()
    {
        QTest::addColumn<QString>("winner");
        QTest::addColumn<QByteArray>("line");

        QTest::newRow("syslog-iso")
            << "syslog-iso"
            << QByteArray("2026-07-19T00:05:01.032005-04:00 psorensen2404 CRON[177922]: "
                          "pam_unix(cron:session): session opened for user root(uid=0)");
        QTest::newRow("syslog-rfc3164")
            << "syslog-rfc3164"
            << QByteArray("Jul 24 14:30:01 ubuntu-server CRON[12345]: (root) CMD (true)");
        QTest::newRow("dpkg")
            << "dpkg"
            << QByteArray("2026-07-06 12:19:50 status unpacked libsane-hpaio:amd64 "
                          "3.23.12+dfsg0-0ubuntu5");
        QTest::newRow("dmesg")
            << "dmesg"
            << QByteArray("[    1.234567] e1000e 0000:00:1f.6 eth0: "
                          "(PCI Express:2.5GT/s:Width x1) 00:1a:2b:3c:4d:5e");
        QTest::newRow("apt-history")
            << "apt-history"
            << QByteArray("Upgrade: socat:amd64 (1.8.0.0-4build3, 1.8.0.0-4ubuntu0.1)");
        QTest::newRow("apt-term")
            << "apt-term"
            << QByteArray("Preparing to unpack "
                          ".../0-printer-driver-postscript-hp_3.23.12+dfsg0-0ubuntu5.1_amd64.deb ...");
        // A pure logging line matches both cloud-init specs at 100%; the
        // specificity gap (0.9 vs 0.85) must send it to cloud-init...
        QTest::newRow("cloud-init")
            << "cloud-init"
            << QByteArray("2025-04-02 15:33:09,851 - main.py[INFO]: "
                          "PID [1] started cloud-init 'init-local'.");
        // ...while banner lines match only cloud-init-output.
        QTest::newRow("cloud-init-output")
            << "cloud-init-output"
            << QByteArray("Cloud-init v. 24.4.1-0ubuntu0~24.04.2 running 'init-local' "
                          "at Wed, 02 Apr 2025 15:33:09 +0000. Up 14.55 seconds.");
        QTest::newRow("apport")
            << "apport"
            << QByteArray("INFO: apport (pid 1090270) 2026-07-24 11:58:36,091: "
                          "called for global pid 640427, signal 6, core limit 0, dump mode 1");
        // Marker lines match dmesg's looser pattern too; xorg's higher
        // specificity (0.95 vs 0.9) must win.
        QTest::newRow("xorg")
            << "xorg"
            << QByteArray("[    26.240] (II) The server relies on udev to provide "
                          "the list of input devices.");
        QTest::newRow("alternatives")
            << "alternatives"
            << QByteArray("update-alternatives 2026-07-07 06:43:30: run with "
                          "--install /usr/bin/rview rview /usr/bin/vim.basic 30");
        QTest::newRow("jsonlines")
            << "jsonlines"
            << QByteArray("{\"__REALTIME_TIMESTAMP\":\"1784922714673521\",\"PRIORITY\":\"6\","
                          "\"SYSLOG_IDENTIFIER\":\"systemd\",\"MESSAGE\":\"Started ollama.service\"}");
    }

    void detection()
    {
        QFETCH(QString, winner);
        QFETCH(QByteArray, line);

        QTemporaryDir dir;
        auto o = openContent(dir, "sample.log", repeatLines(line, 250));
        QVERIFY(o.source && o.index);
        const auto scores = detectFormat(*o.source, *o.index, m_parsers);
        QVERIFY(!scores.isEmpty());
        QCOMPARE(scores.front().parserId, winner);
    }
};

QTEST_APPLESS_MAIN(tst_SystemFormats)
#include "tst_systemformats.moc"
