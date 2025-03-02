#include "clftablemodel.h"

CLFTableModel::CLFTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int CLFTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleRows.size();
}

int CLFTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 10; // Number of columns in CLFColumn enum
}

QVariant CLFTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_visibleRows.size())
        return QVariant();

    const CLFEntry& entry = m_entries[m_visibleRows[index.row()]];

    if (role == Qt::DisplayRole) {
        switch (static_cast<CLFColumn>(index.column())) {
            case CLFColumn::No:
                return m_visibleRows[index.row()] + 1;
            case CLFColumn::RemoteHost:
                return entry.remoteHost;
            case CLFColumn::Identity:
                return entry.remoteLogname;
            case CLFColumn::UserId:
                return entry.userId;
            case CLFColumn::Timestamp:
                return entry.timestamp.toString("dd/MMM/yyyy:HH:mm:ss");
            case CLFColumn::Method:
                return entry.method;
            case CLFColumn::Path:
                return entry.path;
            case CLFColumn::Protocol:
                return entry.protocol;
            case CLFColumn::Status:
                return entry.statusCode;
            case CLFColumn::BytesSent:
                return entry.bytesSent;
            default:
                return QVariant();
        }
    }
    else if (role == Qt::TextAlignmentRole) {
        switch (static_cast<CLFColumn>(index.column())) {
            case CLFColumn::No:
            case CLFColumn::Status:
            case CLFColumn::BytesSent:
                return int(Qt::AlignRight | Qt::AlignVCenter);
            default:
                return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

QVariant CLFTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        switch (static_cast<CLFColumn>(section)) {
            case CLFColumn::No:
                return tr("#");
            case CLFColumn::RemoteHost:
                return tr("Remote Host");
            case CLFColumn::Identity:
                return tr("Identity");
            case CLFColumn::UserId:
                return tr("User ID");
            case CLFColumn::Timestamp:
                return tr("Timestamp");
            case CLFColumn::Method:
                return tr("Method");
            case CLFColumn::Path:
                return tr("Path");
            case CLFColumn::Protocol:
                return tr("Protocol");
            case CLFColumn::Status:
                return tr("Status");
            case CLFColumn::BytesSent:
                return tr("Bytes");
            default:
                return QVariant();
        }
    }
    return QVariant();
}

void CLFTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    m_entries.clear();
    m_entries.reserve(entries.size());
    for (const auto& entry : entries) {
        m_entries.append(CLFEntry::fromLogEntry(entry));
    }
    m_visibleRows.resize(m_entries.size());
    std::iota(m_visibleRows.begin(), m_visibleRows.end(), 0);
    endResetModel();
}

void CLFTableModel::setFilter(const FilterOptions& options)
{
    beginResetModel();
    applyFilter(options);
    endResetModel();
}

bool CLFTableModel::matchesFilter(const CLFEntry& entry, const FilterOptions& options) const
{
    if (options.query.isEmpty())
        return true;

    // Search in all relevant fields
    QString searchText = entry.remoteHost + " " +
                        entry.remoteLogname + " " +
                        entry.userId + " " +
                        entry.timestamp.toString() + " " +
                        entry.request + " " +
                        QString::number(entry.statusCode) + " " +
                        QString::number(entry.bytesSent);

    bool matches = searchText.contains(options.query, options.caseSensitivity);
    return options.invertFilter ? !matches : matches;
}

void CLFTableModel::applyFilter(const FilterOptions& options)
{
    m_visibleRows.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        if (matchesFilter(m_entries[i], options)) {
            m_visibleRows.append(i);
        }
    }
}
