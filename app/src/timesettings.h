#ifndef TIMESETTINGS_H
#define TIMESETTINGS_H

#include <logdor/TimestampParse.h>

#include <QObject>
#include <QTimeZone>

/**
 * App-wide policy for interpreting timestamps that don't say enough on their
 * own: which zone to assume for zone-less formats (RFC3164, logcat, ...) and
 * the reference date for year-less ones. QSettings-backed ("time/assumedZone":
 * empty = the system zone, else "UTC" or an IANA id). Viewers rebuild their
 * extracted timestamp columns on assumedZoneChanged().
 */
class Q_DECL_EXPORT TimeSettings : public QObject {
    Q_OBJECT
public:
    static TimeSettings& instance();

    /// Empty = system zone; otherwise "UTC" or an IANA id.
    QByteArray assumedZoneId() const { return m_zoneId; }
    void setAssumedZoneId(const QByteArray& id);
    QTimeZone assumedZone() const;

    /// Context for parsing @p filePath's timestamps: the assumed zone plus a
    /// reference year/month from the file's mtime (year-less formats infer
    /// their year from it); falls back to today when the file is unknown.
    logdor::TimeParseContext contextForFile(const QString& filePath) const;

signals:
    void assumedZoneChanged();

private:
    TimeSettings();

    QByteArray m_zoneId;
};

#endif // TIMESETTINGS_H
