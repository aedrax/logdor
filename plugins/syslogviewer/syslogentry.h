#ifndef SYSLOGENTRY_H
#define SYSLOGENTRY_H

#include <QtCore/QChar>
#include <QtGui/QColor>
#include <QtCore/QString>
#include <QtCore/QRegularExpression>
#include "../../app/src/plugininterface.h"

class SyslogEntry {
public:
    enum class Facility {
        Kern,      // 0  - Kernel messages
        User,      // 1  - User-level messages
        Mail,      // 2  - Mail system
        Daemon,    // 3  - System daemons
        Auth,      // 4  - Security/authorization
        Syslog,    // 5  - Syslogd messages
        Lpr,       // 6  - Line printer subsystem
        News,      // 7  - Network news subsystem
        Uucp,      // 8  - UUCP subsystem
        Cron,      // 9  - Clock daemon
        AuthPriv,  // 10 - Security/authorization
        Ftp,       // 11 - FTP daemon
        Ntp,       // 12 - NTP subsystem
        LogAudit,  // 13 - Log audit
        LogAlert,  // 14 - Log alert
        Clock,     // 15 - Clock daemon
        Local0,    // 16 - Local use 0
        Local1,    // 17 - Local use 1
        Local2,    // 18 - Local use 2
        Local3,    // 19 - Local use 3
        Local4,    // 20 - Local use 4
        Local5,    // 21 - Local use 5
        Local6,    // 22 - Local use 6
        Local7,    // 23 - Local use 7
        Unknown    // Unknown facility
    };

    enum class Severity {
        Emergency,  // 0 - System is unusable
        Alert,      // 1 - Action must be taken immediately
        Critical,   // 2 - Critical conditions
        Error,      // 3 - Error conditions
        Warning,    // 4 - Warning conditions
        Notice,     // 5 - Normal but significant condition
        Info,       // 6 - Informational messages
        Debug,      // 7 - Debug-level messages
        Unknown     // Unknown severity
    };

    QString timestamp;
    QString hostname;
    QString processName;
    QString pid;
    Facility facility;
    Severity severity;
    QString message;

    SyslogEntry(const LogEntry& entry)
    {
        facility = Facility::Unknown;
        severity = Severity::Unknown;
        parseEntry(entry.getMessage());
    }

private:
    void parseEntry(const QString& text) {
        // Try different syslog formats
        if (parseRFC5424(text)) return;  // Modern format
        if (parseRFC3164(text)) return;  // Legacy/BSD format
        if (parseSimple(text)) return;   // Simple format
        
        // If no format matches, store as raw message
        message = text;
    }

    bool parseRFC5424(const QString& text) {
        // RFC5424: [<PRI>VERSION] TIMESTAMP HOSTNAME APP-NAME PROCID MSGID STRUCTURED-DATA MSG
        static QRegularExpression re(R"((?:                                     # Start optional priority/version
            <(\d+)>                                                             # Priority (optional)
            (\d+)?\s+                                                           # Version (optional)
            )?                                                                  # End optional priority/version
            (\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}                                # Timestamp
            (?:\.\d+)?                                                          # Optional fractional seconds
            (?:Z|[-+]\d{2}:?\d{2})?)\s+                                         # Timezone
            ([^\s]+)\s+                                                         # Hostname
            ([^\s]+)\s+                                                         # App-Name
            ([^\s]+)\s+                                                         # ProcID
            ([^\s]+)\s+                                                         # MsgID
            ((?:-|\[(?:[^\]\\]|\\.)*\](?:\s+\[(?:[^\]\\]|\\.)*\])*)?)\s+        # Structured-Data
            (.+)                                                                # Message
            )", QRegularExpression::ExtendedPatternSyntaxOption);
        
        auto match = re.match(text);
        if (match.hasMatch()) {
            if (!match.captured(1).isEmpty()) {
                int pri = match.captured(1).toInt();
                parsePriority(pri);
            } else {
                facility = Facility::User;  // Default facility
                severity = Severity::Notice;  // Default severity
            }
            
            timestamp = match.captured(3);
            hostname = match.captured(4);
            processName = match.captured(5);
            pid = match.captured(6);
            // msgId = match.captured(7); // Could store this if needed
            // structuredData = match.captured(8); // Could parse this if needed
            message = match.captured(9);
            return true;
        }
        return false;
    }

    bool parseRFC3164(const QString& text) {
        // RFC3164: [<PRI>] TIMESTAMP HOSTNAME TAG[PID]: MSG
        static QRegularExpression re(R"((?:                                     # Start optional priority
            <(\d+)>                                                             # Priority (optional)
            )?                                                                  # End optional priority
            (?:                                                                 # Non-capturing group for timestamp
                (?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\s+          # Month
                (?:[1-9]|[12]\d|3[01])\s+                                       # Day
                (?:[01]\d|2[0-3]):[0-5]\d:[0-5]\d                               # Time
            )\s+
            ([^\s]+)\s+                                                         # Hostname
            ([^:\[]+)                                                           # Process name
            (?:\[(\d+)\])?                                                      # Optional PID
            :\s+
            (.+)                                                                # Message
            )", QRegularExpression::ExtendedPatternSyntaxOption);
        
        auto match = re.match(text);
        if (match.hasMatch()) {
            if (!match.captured(1).isEmpty()) {
                int pri = match.captured(1).toInt();
                parsePriority(pri);
            } else {
                facility = Facility::User;  // Default facility
                severity = Severity::Notice;  // Default severity
            }
            
            timestamp = match.captured(2);
            hostname = match.captured(3);
            processName = match.captured(4);
            pid = match.captured(5);
            message = match.captured(6);
            return true;
        }
        return false;
    }

    bool parseSimple(const QString& text) {
        // Simple format: TIMESTAMP HOSTNAME TAG[PID]: MSG
        static QRegularExpression re(R"((\w+\s+\d+\s+\d{2}:\d{2}:\d{2})\s+([^\s]+)\s+([^:\[]+)(?:\[(\d+)\])?:\s+(.*))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            timestamp = match.captured(1);
            hostname = match.captured(2);
            processName = match.captured(3);
            pid = match.captured(4);
            message = match.captured(5);
            // Default to user.notice if no priority specified
            facility = Facility::User;
            severity = Severity::Notice;
            return true;
        }
        return false;
    }

    void parsePriority(int pri) {
        // Priority value = (facility * 8) + severity
        facility = static_cast<Facility>(pri >> 3);
        severity = static_cast<Severity>(pri & 0x07);
    }

public:
    static QString facilityToString(Facility facility) {
        switch (facility) {
            case Facility::Kern: return "kern";
            case Facility::User: return "user";
            case Facility::Mail: return "mail";
            case Facility::Daemon: return "daemon";
            case Facility::Auth: return "auth";
            case Facility::Syslog: return "syslog";
            case Facility::Lpr: return "lpr";
            case Facility::News: return "news";
            case Facility::Uucp: return "uucp";
            case Facility::Cron: return "cron";
            case Facility::AuthPriv: return "authpriv";
            case Facility::Ftp: return "ftp";
            case Facility::Ntp: return "ntp";
            case Facility::LogAudit: return "logaudit";
            case Facility::LogAlert: return "logalert";
            case Facility::Clock: return "clock";
            case Facility::Local0: return "local0";
            case Facility::Local1: return "local1";
            case Facility::Local2: return "local2";
            case Facility::Local3: return "local3";
            case Facility::Local4: return "local4";
            case Facility::Local5: return "local5";
            case Facility::Local6: return "local6";
            case Facility::Local7: return "local7";
            default: return "unknown";
        }
    }

    static QString severityToString(Severity severity) {
        switch (severity) {
            case Severity::Emergency: return "Emergency";
            case Severity::Alert: return "Alert";
            case Severity::Critical: return "Critical";
            case Severity::Error: return "Error";
            case Severity::Warning: return "Warning";
            case Severity::Notice: return "Notice";
            case Severity::Info: return "Info";
            case Severity::Debug: return "Debug";
            default: return "Unknown";
        }
    }

    static QColor severityColor(Severity severity) {
        switch (severity) {
            case Severity::Emergency: return QColor(139, 0, 0);      // Dark Red
            case Severity::Alert: return QColor(255, 0, 0);         // Red
            case Severity::Critical: return QColor(255, 69, 0);     // Red-Orange
            case Severity::Error: return QColor(255, 140, 0);       // Dark Orange
            case Severity::Warning: return QColor(255, 215, 0);     // Gold
            case Severity::Notice: return QColor(32, 178, 170);     // Light Sea Green
            case Severity::Info: return QColor(100, 149, 237);      // Cornflower Blue
            case Severity::Debug: return QColor(169, 169, 169);     // Dark Gray
            default: return QColor(255, 255, 255);                  // White
        }
    }
};

#endif // SYSLOGENTRY_H
