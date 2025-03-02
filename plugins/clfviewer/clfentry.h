#ifndef CLFENTRY_H
#define CLFENTRY_H

#include <QString>
#include <QDateTime>
#include <QRegularExpression>
#include "../../app/src/plugininterface.h"

struct CLFEntry {
    QString remoteHost;      // IP address of client
    QString remoteLogname;   // RFC 1413 identity
    QString userId;          // User ID
    QDateTime timestamp;     // Access time
    QString request;         // Full HTTP request
    QString method;          // HTTP method (parsed from request)
    QString path;           // Request path (parsed from request)
    QString protocol;       // HTTP protocol (parsed from request)
    int statusCode;         // HTTP status code
    qint64 bytesSent;       // Size of response in bytes

    static CLFEntry fromLogEntry(const LogEntry& entry) {
        CLFEntry clfEntry;
        QString line = entry.getMessage();
        
        // Parse using regex to handle quoted fields and spaces correctly
        static QRegularExpression re("^(\\S+) (\\S+) (\\S+) \\[([\\w:/]+\\s[+\\-]\\d{4})\\] \"([^\"]*)\" (\\d{3}) (\\d+|-)");
        auto match = re.match(line);
        
        if (match.hasMatch()) {
            clfEntry.remoteHost = match.captured(1);
            clfEntry.remoteLogname = match.captured(2);
            clfEntry.userId = match.captured(3);
            
            // Parse timestamp [dd/MMM/yyyy:HH:mm:ss +zone]
            QString timestamp = match.captured(4);
            // Parse timestamp like "10/Dec/2019:13:55:36 -0700"
            QDateTime ts = QDateTime::fromString(timestamp, "dd/MMM/yyyy:HH:mm:ss t");
            if (ts.isValid()) {
                clfEntry.timestamp = ts;
            } else {
                // Try alternate format with timezone offset
                ts = QDateTime::fromString(timestamp, "dd/MMM/yyyy:HH:mm:ss");
                if (ts.isValid()) {
                    // Extract timezone offset
                    QRegularExpression tzRe("\\s([+-]\\d{4})$");
                    auto tzMatch = tzRe.match(timestamp);
                    if (tzMatch.hasMatch()) {
                        QString offset = tzMatch.captured(1);
                        int hours = offset.mid(1,2).toInt();
                        int minutes = offset.mid(3,2).toInt();
                        if (offset.startsWith('-')) {
                            hours = -hours;
                            minutes = -minutes;
                        }
                        ts = ts.addSecs(hours * 3600 + minutes * 60);
                    }
                    clfEntry.timestamp = ts;
                }
            }
            
            // Parse request components
            clfEntry.request = match.captured(5);
            QStringList requestParts = clfEntry.request.split(' ');
            if (requestParts.size() >= 3) {
                clfEntry.method = requestParts[0];
                clfEntry.path = requestParts[1];
                clfEntry.protocol = requestParts[2];
            }
            
            clfEntry.statusCode = match.captured(6).toInt();
            
            // Handle '-' for empty byte count
            QString bytes = match.captured(7);
            clfEntry.bytesSent = bytes == "-" ? 0 : bytes.toLongLong();
        }
        
        return clfEntry;
    }
};

#endif // CLFENTRY_H
