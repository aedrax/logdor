#include <logdor/DeclarativeParser.h>
#include <logdor/FileSource.h>
#include <logdor/FormatRegistry.h>
#include <logdor/FormatSpec.h>
#include <logdor/LineIndexer.h>
#include <logdor/TimestampParse.h>

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
// cloud-init, cloud-init-output, apport, xorg, alternatives, cri, klog,
// apache-error, nginx-error, s3-access, cef, leef, nagios, audit,
// logback, snort), using lines captured from real Ubuntu /var/log files
// or vendor documentation, plus detection checks against the full
// parser list.
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
                                    "alternatives", "cri", "klog", "apache-error",
                                    "nginx-error", "s3-access", "cef", "leef",
                                    "nagios", "audit", "logback", "snort" };
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

        //=== cri: Time, Stream, Tag, Message ================================
        QTest::newRow("cri-stdout-full")
            << "cri"
            << QByteArray("2016-10-06T00:17:09.669794202Z stdout F "
                          "log content 1")
            << QStringList{ "2016-10-06T00:17:09.669794202Z", "stdout", "F",
                            "log content 1" }
            << true << int(Severity::None);
        QTest::newRow("cri-stderr-partial")
            << "cri"
            << QByteArray("2024-03-15T08:22:45.123456789+02:00 stderr P "
                          "partial line without newline")
            << QStringList{ "2024-03-15T08:22:45.123456789+02:00", "stderr", "P",
                            "partial line without newline" }
            << true << int(Severity::None);
        QTest::newRow("cri-empty-message")
            << "cri"
            << QByteArray("2016-10-06T00:17:09.669794202Z stdout F ")
            << QStringList{ "2016-10-06T00:17:09.669794202Z", "stdout", "F", "" }
            << true << int(Severity::None);
        QTest::newRow("cri-nonmatch-syslog-iso")
            << "cri"
            << QByteArray("2026-07-19T00:00:02.026813-04:00 psorensen2404 systemd[1]: "
                          "rsyslog.service: Sent signal SIGHUP to main process 2713")
            << QStringList{ "", "", "",
                            "2026-07-19T00:00:02.026813-04:00 psorensen2404 systemd[1]: "
                            "rsyslog.service: Sent signal SIGHUP to main process 2713" }
            << false << int(Severity::None);

        //=== klog: Level, Time, Thread, Location, Message ===================
        QTest::newRow("klog-info")
            << "klog"
            << QByteArray("I0203 12:34:56.789012   12345 controller.go:123] "
                          "Starting workers of ReplicaSet controller")
            << QStringList{ "I", "0203 12:34:56.789012", "12345",
                            "controller.go:123",
                            "Starting workers of ReplicaSet controller" }
            << true << int(Severity::Info);
        QTest::newRow("klog-warning")
            << "klog"
            << QByteArray("W0203 12:34:57.000001       7 reflector.go:324] "
                          "watch of *v1.Pod ended with: too old resource version")
            << QStringList{ "W", "0203 12:34:57.000001", "7", "reflector.go:324",
                            "watch of *v1.Pod ended with: too old resource version" }
            << true << int(Severity::Warning);
        QTest::newRow("klog-error")
            << "klog"
            << QByteArray("E0715 08:00:00.123456 1834547 kubelet.go:2879] "
                          "Container runtime network not ready")
            << QStringList{ "E", "0715 08:00:00.123456", "1834547",
                            "kubelet.go:2879",
                            "Container runtime network not ready" }
            << true << int(Severity::Error);
        QTest::newRow("klog-fatal-empty-message")
            << "klog"
            << QByteArray("F0203 23:59:59.999999 1 main.go:1] ")
            << QStringList{ "F", "0203 23:59:59.999999", "1", "main.go:1", "" }
            << true << int(Severity::Fatal);
        QTest::newRow("klog-nonmatch-no-bracket")
            << "klog"
            << QByteArray("I0203 12:34:56.789012 12345 no closing bracket")
            << QStringList{ "", "", "", "",
                            "I0203 12:34:56.789012 12345 no closing bracket" }
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

        //=== apache-error: Time, Module, Level, PID, TID, Client, Message ===
        // The bracket time is asctime-with-usec plus a trailing year; the
        // capture keeps ms precision and drops weekday/year so the value fits
        // the Rfc3164 codec (year inferred from file mtime).
        QTest::newRow("apache-error-24-notice")
            << "apache-error"
            << QByteArray("[Thu Jun 27 11:55:44.569531 2024] [core:notice] "
                          "[pid 1234:tid 5678] AH00094: Command line: '/usr/sbin/apache2'")
            << QStringList{ "Jun 27 11:55:44.569", "core", "notice", "1234", "5678",
                            "", "AH00094: Command line: '/usr/sbin/apache2'" }
            << true << int(Severity::Info);
        QTest::newRow("apache-error-24-client")
            << "apache-error"
            << QByteArray("[Fri Jun 28 09:02:01.123456 2024] [authz_core:error] "
                          "[pid 77:tid 88] [client 192.0.2.9:51034] "
                          "AH01630: client denied by server configuration: /var/www/secret")
            << QStringList{ "Jun 28 09:02:01.123", "authz_core", "error", "77", "88",
                            "192.0.2.9:51034",
                            "AH01630: client denied by server configuration: /var/www/secret" }
            << true << int(Severity::Error);
        // Apache 2.2 shape: no module, no pid/tid, second-resolution time.
        QTest::newRow("apache-error-22-no-module")
            << "apache-error"
            << QByteArray("[Thu Jun 27 11:55:44 2024] [error] [client 192.168.1.1] "
                          "File does not exist: /var/www/favicon.ico")
            << QStringList{ "Jun 27 11:55:44", "", "error", "", "", "192.168.1.1",
                            "File does not exist: /var/www/favicon.ico" }
            << true << int(Severity::Error);
        QTest::newRow("apache-error-nonmatch")
            << "apache-error"
            << QByteArray("AH00558: apache2: Could not reliably determine the "
                          "server's fully qualified domain name")
            << QStringList{ "", "", "", "", "", "",
                            "AH00558: apache2: Could not reliably determine the "
                            "server's fully qualified domain name" }
            << false << int(Severity::None);

        //=== nginx-error: Time, Level, PID, TID, Connection, Message ========
        QTest::newRow("nginx-error-conn")
            << "nginx-error"
            << QByteArray("2024/06/27 11:55:44 [error] 1234#5678: *90 open() "
                          "\"/usr/share/nginx/html/favicon.ico\" failed "
                          "(2: No such file or directory), client: 192.0.2.1, "
                          "server: example.com, request: \"GET /favicon.ico HTTP/1.1\"")
            << QStringList{ "2024/06/27 11:55:44", "error", "1234", "5678", "90",
                            "open() \"/usr/share/nginx/html/favicon.ico\" failed "
                            "(2: No such file or directory), client: 192.0.2.1, "
                            "server: example.com, request: \"GET /favicon.ico HTTP/1.1\"" }
            << true << int(Severity::Error);
        QTest::newRow("nginx-error-notice-no-conn")
            << "nginx-error"
            << QByteArray("2024/06/27 11:55:40 [notice] 1234#1234: "
                          "using the \"epoll\" event method")
            << QStringList{ "2024/06/27 11:55:40", "notice", "1234", "1234", "",
                            "using the \"epoll\" event method" }
            << true << int(Severity::Info);
        QTest::newRow("nginx-error-nonmatch-clf")
            << "nginx-error"
            << QByteArray("127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "
                          "\"GET /apache_pb.gif HTTP/1.0\" 200 2326")
            << QStringList{ "", "", "", "", "",
                            "127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "
                            "\"GET /apache_pb.gif HTTP/1.0\" 200 2326" }
            << false << int(Severity::None);

        //=== s3-access: 18 classic fields + Extra (post-2019 tail) ==========
        // The AWS documentation example line, with the newer trailing fields
        // lumped into Extra so pre-2019 lines still match.
        QTest::newRow("s3-access-full-2019")
            << "s3-access"
            << QByteArray("79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be "
                          "awsexamplebucket1 [06/Feb/2019:00:00:38 +0000] 192.0.2.3 "
                          "79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be "
                          "3E57427F3EXAMPLE REST.GET.VERSIONING - "
                          "\"GET /awsexamplebucket1?versioning HTTP/1.1\" 200 - 113 - 7 - \"-\" "
                          "\"S3Console/0.4\" - "
                          "s9lzHYrFp76ZVxRcpX9+5cjAnEH2ROuNkd2BHfIa6UkFVdtjf5mKR3/eTPFvsiP/XV/VLi31234= "
                          "SigV2 ECDHE-RSA-AES128-GCM-SHA256 AuthHeader "
                          "awsexamplebucket1.s3.us-west-1.amazonaws.com TLSV1.1 -")
            << QStringList{ "79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be",
                            "awsexamplebucket1", "06/Feb/2019:00:00:38 +0000", "192.0.2.3",
                            "79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be",
                            "3E57427F3EXAMPLE", "REST.GET.VERSIONING", "-",
                            "GET /awsexamplebucket1?versioning HTTP/1.1", "200", "-",
                            "113", "-", "7", "-", "-", "S3Console/0.4", "-",
                            "s9lzHYrFp76ZVxRcpX9+5cjAnEH2ROuNkd2BHfIa6UkFVdtjf5mKR3/eTPFvsiP/XV/VLi31234= "
                            "SigV2 ECDHE-RSA-AES128-GCM-SHA256 AuthHeader "
                            "awsexamplebucket1.s3.us-west-1.amazonaws.com TLSV1.1 -" }
            << true << int(Severity::None);
        QTest::newRow("s3-access-legacy-18-field")
            << "s3-access"
            << QByteArray("79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be "
                          "awsexamplebucket1 [06/Feb/2019:00:00:38 +0000] 192.0.2.3 "
                          "79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be "
                          "891CE47D2EXAMPLE REST.GET.LOGGING_STATUS - "
                          "\"GET /awsexamplebucket1?logging HTTP/1.1\" 200 - 242 - 11 - \"-\" "
                          "\"S3Console/0.4\" -")
            << QStringList{ "79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be",
                            "awsexamplebucket1", "06/Feb/2019:00:00:38 +0000", "192.0.2.3",
                            "79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be",
                            "891CE47D2EXAMPLE", "REST.GET.LOGGING_STATUS", "-",
                            "GET /awsexamplebucket1?logging HTTP/1.1", "200", "-",
                            "242", "-", "11", "-", "-", "S3Console/0.4", "-", "" }
            << true << int(Severity::None);
        // Combined-format web logs have three tokens before the bracket, S3
        // exactly two - the differentiator that keeps clf and s3-access apart.
        QTest::newRow("s3-access-nonmatch-clf")
            << "s3-access"
            << QByteArray("127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "
                          "\"GET /apache_pb.gif HTTP/1.0\" 200 2326 "
                          "\"http://www.example.com/start.html\" \"Mozilla/4.08 [en] (Win98; I ;Nav)\"")
            << QStringList{ "", "", "", "", "", "", "", "",
                            "127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "
                            "\"GET /apache_pb.gif HTTP/1.0\" 200 2326 "
                            "\"http://www.example.com/start.html\" \"Mozilla/4.08 [en] (Win98; I ;Nav)\"",
                            "", "", "", "", "", "", "", "", "", "" }
            << false << int(Severity::None);

        //=== cef: Time, Host, CEF Version, Vendor, Product, Product Version,
        //         Signature ID, Name, Severity, Extensions ==================
        QTest::newRow("cef-syslog-prefixed")
            << "cef"
            << QByteArray("Sep 19 08:26:10 zurich CEF:0|security|threatmanager|1.0|100|"
                          "worm successfully stopped|10|src=10.0.0.1 dst=2.1.2.2 spt=1232")
            << QStringList{ "Sep 19 08:26:10", "zurich", "0", "security",
                            "threatmanager", "1.0", "100", "worm successfully stopped",
                            "10", "src=10.0.0.1 dst=2.1.2.2 spt=1232" }
            << true << int(Severity::Fatal);
        // Escaped pipes in header values are kept raw (\| is not unescaped).
        QTest::newRow("cef-bare-escaped-pipe")
            << "cef"
            << QByteArray("CEF:0|Vendor|Product|1.0|42|detected a threat \\| escaped|Low|"
                          "msg=hello")
            << QStringList{ "", "", "0", "Vendor", "Product", "1.0", "42",
                            "detected a threat \\| escaped", "Low", "msg=hello" }
            << true << int(Severity::Info);
        QTest::newRow("cef-nonmatch-syslog")
            << "cef"
            << QByteArray("Jul 24 14:30:01 ubuntu-server CRON[12345]: (root) CMD (true)")
            << QStringList{ "", "", "", "", "", "", "", "", "",
                            "Jul 24 14:30:01 ubuntu-server CRON[12345]: (root) CMD (true)" }
            << false << int(Severity::None);

        //=== leef: Time, Host, LEEF Version, Vendor, Product, Product Version,
        //          Event ID, Delimiter, Extensions ==========================
        // LEEF 1.0: five header pipes, tab-separated extension, no delimiter
        // field (the optional delim group needs a short pipe-terminated token
        // without '=', which a key=value extension never provides).
        QTest::newRow("leef-10-tabs")
            << "leef"
            << QByteArray("Jan 18 11:07:53 host1 LEEF:1.0|Microsoft|MSExchange|4.0 SP1|"
                          "15345|src=192.0.2.0\tdst=172.50.123.1\tsev=5\tcat=anomaly")
            << QStringList{ "Jan 18 11:07:53", "host1", "1.0", "Microsoft",
                            "MSExchange", "4.0 SP1", "15345", "",
                            "src=192.0.2.0\tdst=172.50.123.1\tsev=5\tcat=anomaly" }
            << true << int(Severity::None);
        QTest::newRow("leef-20-caret-delim")
            << "leef"
            << QByteArray("LEEF:2.0|Lancope|StealthWatch|1.0|41|^|src=10.0.1.8^dst=10.0.0.5")
            << QStringList{ "", "", "2.0", "Lancope", "StealthWatch", "1.0", "41",
                            "^", "src=10.0.1.8^dst=10.0.0.5" }
            << true << int(Severity::None);
        QTest::newRow("leef-nonmatch")
            << "leef"
            << QByteArray("Jul 24 14:30:01 ubuntu-server CRON[12345]: (root) CMD (true)")
            << QStringList{ "", "", "", "", "", "", "", "",
                            "Jul 24 14:30:01 ubuntu-server CRON[12345]: (root) CMD (true)" }
            << false << int(Severity::None);

        //=== nagios: Time, Type, Message ====================================
        QTest::newRow("nagios-service-alert")
            << "nagios"
            << QByteArray("[1700000000] SERVICE ALERT: "
                          "web01;HTTP;CRITICAL;SOFT;1;CRITICAL - Socket timeout after 10 seconds")
            << QStringList{ "1700000000", "SERVICE ALERT",
                            "web01;HTTP;CRITICAL;SOFT;1;CRITICAL - Socket timeout after 10 seconds" }
            << true << int(Severity::None);
        QTest::newRow("nagios-external-command")
            << "nagios"
            << QByteArray("[1700000030] EXTERNAL COMMAND: "
                          "PROCESS_SERVICE_CHECK_RESULT;web01;HTTP;0;OK")
            << QStringList{ "1700000030", "EXTERNAL COMMAND",
                            "PROCESS_SERVICE_CHECK_RESULT;web01;HTTP;0;OK" }
            << true << int(Severity::None);
        // Daemon lines have no TYPE: prefix; Type stays empty.
        QTest::newRow("nagios-typeless")
            << "nagios"
            << QByteArray("[1700000060] Caught SIGTERM, shutting down...")
            << QStringList{ "1700000060", "", "Caught SIGTERM, shutting down..." }
            << true << int(Severity::None);
        QTest::newRow("nagios-nonmatch")
            << "nagios"
            << QByteArray("Nagios Core 4.4.6 starting... (PID=1234)")
            << QStringList{ "", "", "Nagios Core 4.4.6 starting... (PID=1234)" }
            << false << int(Severity::None);

        //=== audit: Node, Type, Time, Serial, Message =======================
        QTest::newRow("audit-syscall")
            << "audit"
            << QByteArray("type=SYSCALL msg=audit(1364475353.159:24270): "
                          "arch=c000003e syscall=2 success=yes exit=3")
            << QStringList{ "", "SYSCALL", "1364475353.159", "24270",
                            "arch=c000003e syscall=2 success=yes exit=3" }
            << true << int(Severity::None);
        QTest::newRow("audit-node-prefixed")
            << "audit"
            << QByteArray("node=host1 type=USER_LOGIN msg=audit(1700000000.123:456): "
                          "pid=1234 uid=0 auid=1000 ses=2 res=success")
            << QStringList{ "host1", "USER_LOGIN", "1700000000.123", "456",
                            "pid=1234 uid=0 auid=1000 ses=2 res=success" }
            << true << int(Severity::None);
        // logfmt lines start key=value too; the msg=audit(...) shape is the
        // differentiator (keyvalue itself is anchored on ts=).
        QTest::newRow("audit-nonmatch-logfmt")
            << "audit"
            << QByteArray("ts=2026-07-01T10:00:00Z level=info msg=\"hello\"")
            << QStringList{ "", "", "", "",
                            "ts=2026-07-01T10:00:00Z level=info msg=\"hello\"" }
            << false << int(Severity::None);

        //=== logback: Time, Thread, Level, Logger, Message ==================
        QTest::newRow("logback-comma-ms")
            << "logback"
            << QByteArray("2024-06-27 11:55:44,123 [main] INFO  "
                          "com.example.app.Application - Started Application in 2.312 seconds")
            << QStringList{ "2024-06-27 11:55:44,123", "main", "INFO",
                            "com.example.app.Application",
                            "Started Application in 2.312 seconds" }
            << true << int(Severity::Info);
        QTest::newRow("logback-dot-ms-T-sep")
            << "logback"
            << QByteArray("2024-06-27T11:55:45.001 [http-nio-8080-exec-3] ERROR "
                          "c.e.a.web.ApiController - Unhandled exception")
            << QStringList{ "2024-06-27T11:55:45.001", "http-nio-8080-exec-3",
                            "ERROR", "c.e.a.web.ApiController",
                            "Unhandled exception" }
            << true << int(Severity::Error);
        // Stack-trace continuation lines fall back to the message column;
        // the bare HH:mm:ss.SSS no-config logback default is out of scope
        // (no codec parses a time-of-day-only value).
        QTest::newRow("logback-nonmatch-stacktrace")
            << "logback"
            << QByteArray("java.lang.NullPointerException: null")
            << QStringList{ "", "", "", "",
                            "java.lang.NullPointerException: null" }
            << false << int(Severity::None);

        //=== snort: Time, Signature, Message, Classification, Priority,
        //           Protocol, Source, Destination ===========================
        QTest::newRow("snort-classified")
            << "snort"
            << QByteArray("01/18-11:07:53.123456  [**] [1:2100498:7] "
                          "GPL ATTACK_RESPONSE id check returned root [**] "
                          "[Classification: Potentially Bad Traffic] [Priority: 2] "
                          "{TCP} 192.0.2.5:80 -> 10.0.0.1:445")
            << QStringList{ "01/18-11:07:53.123456", "1:2100498:7",
                            "GPL ATTACK_RESPONSE id check returned root",
                            "Potentially Bad Traffic", "2", "TCP",
                            "192.0.2.5:80", "10.0.0.1:445" }
            << true << int(Severity::Warning);
        // Classification is optional; year-included snort timestamp configs
        // (01/18/24-...) are out of scope.
        QTest::newRow("snort-no-classification")
            << "snort"
            << QByteArray("01/18-11:08:04.987654 [**] [129:12:1] "
                          "Consecutive TCP small segments exceeding threshold [**] "
                          "[Priority: 3] {TCP} 10.0.0.7:52814 -> 192.0.2.5:443")
            << QStringList{ "01/18-11:08:04.987654", "129:12:1",
                            "Consecutive TCP small segments exceeding threshold",
                            "", "3", "TCP", "10.0.0.7:52814", "192.0.2.5:443" }
            << true << int(Severity::Info);
        QTest::newRow("snort-nonmatch")
            << "snort"
            << QByteArray("Commencing packet processing (pid=1234)")
            << QStringList{ "", "", "Commencing packet processing (pid=1234)",
                            "", "", "", "", "" }
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
        // The message's colon makes syslog-iso match too (host=stdout,
        // proc=F failed); CRI's higher specificity (0.95 vs 0.9) must win.
        QTest::newRow("cri")
            << "cri"
            << QByteArray("2016-10-06T00:17:09.669794202Z stderr F "
                          "E1006 00:17:09.669 pod: sync error: connection refused");
        QTest::newRow("klog")
            << "klog"
            << QByteArray("I0203 12:34:56.789012   12345 controller.go:123] "
                          "Starting workers of ReplicaSet controller");
        // The syslog-iso and jsonlines rows double as the journalctl paths:
        // `journalctl -o short-iso-precise` emits syslog-iso lines and
        // `journalctl -o json` emits jsonlines (issue #30).
        QTest::newRow("jsonlines")
            << "jsonlines"
            << QByteArray("{\"__REALTIME_TIMESTAMP\":\"1784922714673521\",\"PRIORITY\":\"6\","
                          "\"SYSLOG_IDENTIFIER\":\"systemd\",\"MESSAGE\":\"Started ollama.service\"}");
        QTest::newRow("apache-error")
            << "apache-error"
            << QByteArray("[Thu Jun 27 11:55:44.569531 2024] [core:notice] "
                          "[pid 1234:tid 5678] AH00094: Command line: '/usr/sbin/apache2'");
        QTest::newRow("nginx-error")
            << "nginx-error"
            << QByteArray("2024/06/27 11:55:44 [error] 1234#5678: *90 open() "
                          "\"/usr/share/nginx/html/favicon.ico\" failed "
                          "(2: No such file or directory), client: 192.0.2.1");
        QTest::newRow("s3-access")
            << "s3-access"
            << QByteArray("79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be "
                          "awsexamplebucket1 [06/Feb/2019:00:00:38 +0000] 192.0.2.3 "
                          "79a59df900b949e55d96a1e698fbacedfd6e09d98eacf8f8d5218e7cd47ef2be "
                          "3E57427F3EXAMPLE REST.GET.VERSIONING - "
                          "\"GET /awsexamplebucket1?versioning HTTP/1.1\" 200 - 113 - 7 - \"-\" "
                          "\"S3Console/0.4\" -");
        // Syslog-prefixed CEF/LEEF lines fully match the syslog specs too
        // (proc=CEF/LEEF); the 0.95 specificity must out-score them.
        QTest::newRow("cef")
            << "cef"
            << QByteArray("Sep 19 08:26:10 zurich CEF:0|security|threatmanager|1.0|100|"
                          "worm successfully stopped|10|src=10.0.0.1 dst=2.1.2.2 spt=1232");
        QTest::newRow("leef")
            << "leef"
            << QByteArray("Jan 18 11:07:53 host1 LEEF:1.0|Microsoft|MSExchange|4.0 SP1|"
                          "15345|src=192.0.2.0\tdst=172.50.123.1\tsev=5\tcat=anomaly");
        // nginx access logs in the default "combined" format are byte-
        // compatible with Apache combined - the clf builtin owns them
        // (issue #38 needs no nginx-access spec).
        QTest::newRow("nginx-access-combined-is-clf")
            << "clf"
            << QByteArray("192.0.2.1 - - [27/Jun/2024:11:55:44 +0000] "
                          "\"GET /index.html HTTP/1.1\" 200 612 \"-\" "
                          "\"Mozilla/5.0 (X11; Linux x86_64)\"");
        QTest::newRow("nagios")
            << "nagios"
            << QByteArray("[1700000000] SERVICE ALERT: "
                          "web01;HTTP;CRITICAL;SOFT;1;CRITICAL - Socket timeout after 10 seconds");
        QTest::newRow("audit")
            << "audit"
            << QByteArray("type=SYSCALL msg=audit(1364475353.159:24270): "
                          "arch=c000003e syscall=2 success=yes exit=3");
        QTest::newRow("logback")
            << "logback"
            << QByteArray("2024-06-27 11:55:44,123 [main] INFO  "
                          "com.example.app.Application - Started Application in 2.312 seconds");
        QTest::newRow("snort")
            << "snort"
            << QByteArray("01/18-11:07:53.123456  [**] [1:2100498:7] "
                          "GPL ATTACK_RESPONSE id check returned root [**] "
                          "[Classification: Potentially Bad Traffic] [Priority: 2] "
                          "{TCP} 192.0.2.5:80 -> 10.0.0.1:445");
    }

    void timestampCodecs_data()
    {
        QTest::addColumn<QString>("parserId");
        QTest::addColumn<QString>("sample"); // a golden Time value from above

        QTest::newRow("syslog-iso") << "syslog-iso"
                                    << "2026-07-19T00:00:02.026813-04:00";
        QTest::newRow("syslog-rfc3164") << "syslog-rfc3164" << "Jul 24 14:30:01";
        QTest::newRow("rfc3164-single-digit-day") << "syslog-rfc3164"
                                                  << "Jul  1 03:04:05";
        QTest::newRow("dpkg") << "dpkg" << "2026-07-06 12:19:48";
        QTest::newRow("dmesg-uptime") << "dmesg" << "0.000000";
        QTest::newRow("cloud-init-comma-ms") << "cloud-init"
                                             << "2025-04-02 15:33:09,851";
        QTest::newRow("apport") << "apport" << "2026-07-24 11:58:36,091";
        QTest::newRow("xorg-uptime") << "xorg" << "26.235";
        QTest::newRow("alternatives") << "alternatives" << "2026-07-07 06:43:30";
        QTest::newRow("keyvalue") << "keyvalue" << "2026-07-01T10:00:00Z";
        QTest::newRow("logcat") << "logcat" << "07-24 06:15:02.123";
        QTest::newRow("clf") << "clf" << "24/Jul/2026:06:15:02";
        // RFC3339Nano: iso8601 keeps the first three fractional digits.
        QTest::newRow("cri-nano") << "cri" << "2016-10-06T00:17:09.669794202Z";
        // Year-less klog with microsecond fraction (Kind::Klog).
        QTest::newRow("klog") << "klog" << "0203 12:34:56.789012";
        // JsonLines declares no format; auto-detection must resolve its
        // journald-normalized ISO output.
        QTest::newRow("jsonlines-auto") << "jsonlines"
                                        << "2026-07-24T06:15:02.123Z";
        // Declared Rfc3164 kind tolerates the ms fraction the capture keeps.
        QTest::newRow("apache-error") << "apache-error" << "Jun 27 11:55:44.569";
        QTest::newRow("nginx-error") << "nginx-error" << "2024/06/27 11:55:44";
        // The Clf kind consumes the trailing zone offset inside the capture.
        QTest::newRow("s3-access") << "s3-access" << "06/Feb/2019:00:00:38 +0000";
        // CEF/LEEF declare no format; detection resolves the syslog prefix.
        QTest::newRow("cef-auto") << "cef" << "Sep 19 08:26:10";
        QTest::newRow("leef-auto") << "leef" << "Jan 18 11:07:53";
        QTest::newRow("nagios-epoch") << "nagios" << "1700000000";
        // Kind::EpochSeconds accepts fractional seconds.
        QTest::newRow("audit-epoch-frac") << "audit" << "1364475353.159";
        // iso8601 accepts the space separator and comma fraction.
        QTest::newRow("logback-comma") << "logback" << "2024-06-27 11:55:44,123";
        QTest::newRow("snort") << "snort" << "01/18-11:07:53.123456";
    }

    // Every bundled timestamp field must yield a codec (declared format or
    // detection) that parses the real captured values from the golden rows.
    void timestampCodecs()
    {
        QFETCH(QString, parserId);
        QFETCH(QString, sample);

        const auto parser = parserById(parserId, m_parsers);
        QVERIFY(parser);
        const auto schema = parser->schema();
        const auto field = std::find_if(schema.begin(), schema.end(),
                                        [](const FieldSchema& f) {
                                            return f.hint == FieldHint::Timestamp;
                                        });
        QVERIFY(field != schema.end());
        QCOMPARE(field->type, FieldType::DateTime);

        const TimeParseContext ctx { QTimeZone::utc(), 2026, 7 };
        const TimestampCodec codec = field->timeFormat.isEmpty()
            ? TimestampCodec::detect({ sample }, ctx)
            : TimestampCodec::fromFormatString(field->timeFormat, ctx);
        QVERIFY2(codec.isValid(), qPrintable(field->timeFormat));
        qint64 ms = 0;
        QVERIFY2(codec.parse(sample, &ms), qPrintable(sample));
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
