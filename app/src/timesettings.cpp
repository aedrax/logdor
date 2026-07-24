#include "timesettings.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSettings>

TimeSettings& TimeSettings::instance()
{
    static TimeSettings settings;
    return settings;
}

TimeSettings::TimeSettings()
{
    m_zoneId = QSettings("Logdor", "Logdor")
                   .value("time/assumedZone").toByteArray();
}

void TimeSettings::setAssumedZoneId(const QByteArray& id)
{
    if (id == m_zoneId)
        return;
    m_zoneId = id;
    QSettings("Logdor", "Logdor")
        .setValue("time/assumedZone", QString::fromUtf8(id));
    emit assumedZoneChanged();
}

QTimeZone TimeSettings::assumedZone() const
{
    if (m_zoneId.isEmpty())
        return QTimeZone::systemTimeZone();
    const QTimeZone zone(m_zoneId);
    return zone.isValid() ? zone : QTimeZone::systemTimeZone();
}

logdor::TimeParseContext TimeSettings::contextForFile(
    const QString& filePath) const
{
    logdor::TimeParseContext context;
    context.assumedZone = assumedZone();
    QDate reference = QDate::currentDate();
    if (!filePath.isEmpty()) {
        const QDateTime modified = QFileInfo(filePath).lastModified();
        if (modified.isValid())
            reference = modified.date();
    }
    context.referenceYear = reference.year();
    context.referenceMonth = reference.month();
    return context;
}
