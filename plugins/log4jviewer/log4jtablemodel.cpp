#include "log4jtablemodel.h"
#include <QDateTime>
#include <algorithm>

Log4jTableModel::Log4jTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int Log4jTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

int Log4jTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return Column::ColumnCount;
}

QVariant Log4jTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return QVariant();

    const Log4jEntry& entry = m_entries.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (static_cast<Column>(index.column())) {
            case LineNumber:
                return index.row() + 1;
            case Timestamp:
                return entry.timestamp;
            case Level:
                return Log4jEntry::levelToString(entry.level);
            case Thread:
                return entry.thread;
            case Logger:
                if (!entry.file.isEmpty() && !entry.lineNumber.isEmpty()) {
                    return QString("%1 (%2:%3)").arg(entry.logger, entry.file, entry.lineNumber);
                }
                return entry.logger;
            case Message:
                return entry.message + (entry.throwable.isEmpty() ? "" : "\n" + entry.throwable);
            case MDC:
                return entry.mdc;
            default:
                return QVariant();
        }
    }
    else if (role == Qt::BackgroundRole) {
        return Log4jEntry::levelColor(entry.level);
    }
    else if (role == Qt::TextAlignmentRole) {
        switch (static_cast<Column>(index.column())) {
            case LineNumber:
                return int(Qt::AlignRight | Qt::AlignVCenter);
            case Message:
            case MDC:
                return int(Qt::AlignLeft | Qt::AlignVCenter);
            default:
                return Qt::AlignCenter;
        }
    }

    return QVariant();
}

QVariant Log4jTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        return columnName(static_cast<Column>(section));
    }
    return QVariant();
}

void Log4jTableModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    std::sort(m_entries.begin(), m_entries.end(), 
        [column, order](const Log4jEntry& a, const Log4jEntry& b) {
            bool lessThan;
            switch (static_cast<Column>(column)) {
                case LineNumber:
                    return false; // Don't sort line numbers
                case Timestamp:
                    lessThan = a.timestamp < b.timestamp;
                    break;
                case Level: {
                    int levelA = static_cast<int>(a.level);
                    int levelB = static_cast<int>(b.level);
                    lessThan = levelA < levelB;
                    break;
                }
                case Thread:
                    lessThan = a.thread < b.thread;
                    break;
                case Logger:
                    lessThan = a.logger < b.logger;
                    break;
                case Message:
                    lessThan = a.message < b.message;
                    break;
                case MDC:
                    lessThan = a.mdc < b.mdc;
                    break;
                default:
                    return false;
            }
            return order == Qt::AscendingOrder ? lessThan : !lessThan;
        });
    endResetModel();
}

void Log4jTableModel::setEntries(const QList<Log4jEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

QSet<QString> Log4jTableModel::getUniqueLoggers() const
{
    QSet<QString> loggers;
    for (const auto& entry : m_entries) {
        if (!entry.logger.isEmpty()) {
            loggers.insert(entry.logger);
        }
    }
    return loggers;
}

QString Log4jTableModel::columnName(Column column)
{
    switch (column) {
        case LineNumber: return tr("#");
        case Timestamp: return tr("Timestamp");
        case Level: return tr("Level");
        case Thread: return tr("Thread");
        case Logger: return tr("Logger");
        case Message: return tr("Message");
        case MDC: return tr("MDC");
        default: return QString();
    }
}
