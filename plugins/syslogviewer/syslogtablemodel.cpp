#include <QtConcurrent/QtConcurrent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>

#include "syslogtablemodel.h"

SyslogTableModel::SyslogTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int SyslogTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleRows.size();
}

int SyslogTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 7; // No., Time, Hostname, Process, Facility, Severity, Message
}

QVariant SyslogTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_visibleRows.size())
        return QVariant();

    const SyslogEntry& entry(m_entries[m_visibleRows[index.row()]]);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case SyslogColumn::No:
            return m_visibleRows[index.row()] + 1; // Line number (1-based)
        case SyslogColumn::Time:
            return entry.timestamp;
        case SyslogColumn::Hostname:
            return entry.hostname;
        case SyslogColumn::Process:
            return entry.pid.isEmpty() ? entry.processName 
                                     : QString("%1[%2]").arg(entry.processName, entry.pid);
        case SyslogColumn::Facility:
            return SyslogEntry::facilityToString(entry.facility);
        case SyslogColumn::Severity:
            return SyslogEntry::severityToString(entry.severity);
        case SyslogColumn::Message:
            return entry.message;
        }
    }
    else if (role == Qt::BackgroundRole) {
        return SyslogEntry::severityColor(entry.severity);
    }
    else if (role == Qt::ForegroundRole) {
        // Use white text for dark backgrounds (emergency, alert, critical)
        if (entry.severity <= SyslogEntry::Severity::Critical) {
            return QColor(Qt::white);
        }
        return QColor(Qt::black);
    }
    else if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case SyslogColumn::No:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

QVariant SyslogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case SyslogColumn::No: return tr("No.");
    case SyslogColumn::Time: return tr("Time");
    case SyslogColumn::Hostname: return tr("Hostname");
    case SyslogColumn::Process: return tr("Process");
    case SyslogColumn::Facility: return tr("Facility");
    case SyslogColumn::Severity: return tr("Severity");
    case SyslogColumn::Message: return tr("Message");
    default: return QVariant();
    }
}

void SyslogTableModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    
    m_sortColumn = column;
    m_sortOrder = order;
    
    std::sort(m_visibleRows.begin(), m_visibleRows.end(), 
        [this, column, order](int left, int right) {
            const SyslogEntry& leftEntry(m_entries[left]);
            const SyslogEntry& rightEntry(m_entries[right]);
            
            bool lessThan;
            switch (column) {
            case SyslogColumn::No: // Line number
                lessThan = left < right;
                break;
            case SyslogColumn::Time:
                lessThan = leftEntry.timestamp < rightEntry.timestamp;
                break;
            case SyslogColumn::Hostname:
                lessThan = leftEntry.hostname < rightEntry.hostname;
                break;
            case SyslogColumn::Process:
                lessThan = leftEntry.processName < rightEntry.processName;
                break;
            case SyslogColumn::Facility:
                lessThan = leftEntry.facility < rightEntry.facility;
                break;
            case SyslogColumn::Severity:
                lessThan = leftEntry.severity < rightEntry.severity;
                break;
            case SyslogColumn::Message:
                lessThan = leftEntry.message < rightEntry.message;
                break;
            default:
                lessThan = false;
            }
            return order == Qt::AscendingOrder ? lessThan : !lessThan;
        });
    
    endResetModel();
}

void SyslogTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    m_visibleRows.resize(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        m_visibleRows[i] = i;
    }
    m_sortColumn = 0;
    m_sortOrder = Qt::AscendingOrder;
    endResetModel();
}

void SyslogTableModel::setVisibleRows(const QList<int>& linesToShow)
{
    QList<int> sortedLines = linesToShow;
    if (m_sortColumn != 0 || m_sortOrder != Qt::AscendingOrder) {
        std::sort(sortedLines.begin(), sortedLines.end(), 
            [this](int left, int right) {
                const SyslogEntry leftEntry(m_entries[left]);
                const SyslogEntry rightEntry(m_entries[right]);
                
                bool lessThan;
                switch (m_sortColumn) {
                case SyslogColumn::No:
                    lessThan = left < right;
                    break;
                case SyslogColumn::Time:
                    lessThan = leftEntry.timestamp < rightEntry.timestamp;
                    break;
                case SyslogColumn::Hostname:
                    lessThan = leftEntry.hostname < rightEntry.hostname;
                    break;
                case SyslogColumn::Process:
                    lessThan = leftEntry.processName < rightEntry.processName;
                    break;
                case SyslogColumn::Facility:
                    lessThan = leftEntry.facility < rightEntry.facility;
                    break;
                case SyslogColumn::Severity:
                    lessThan = leftEntry.severity < rightEntry.severity;
                    break;
                case SyslogColumn::Message:
                    lessThan = leftEntry.message < rightEntry.message;
                    break;
                default:
                    lessThan = false;
                }
                return m_sortOrder == Qt::AscendingOrder ? lessThan : !lessThan;
            });
    }
    
    beginResetModel();
    m_visibleRows = sortedLines;
    endResetModel();
}

bool SyslogTableModel::matchesFilter(const SyslogEntry& entry, const QString& query,
                                   Qt::CaseSensitivity caseSensitivity,
                                   const QSet<QString>& facilities,
                                   const QMap<SyslogEntry::Severity, bool>& severityFilters) const
{
    // Check facility filter
    if (!facilities.isEmpty() && !facilities.contains(SyslogEntry::facilityToString(entry.facility))) {
        return false;
    }

    // Check severity filter
    if (!severityFilters.isEmpty() && !severityFilters.value(entry.severity, true)) {
        return false;
    }

    // Check text query
    if (!query.isEmpty()) {
        QString text = entry.message;
        text += " " + entry.processName;
        text += " " + entry.hostname;
        text += " " + SyslogEntry::facilityToString(entry.facility);
        text += " " + SyslogEntry::severityToString(entry.severity);
        
        return text.contains(query, caseSensitivity);
    }

    return true;
}
