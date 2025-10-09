#include "logcattablemodel.h"
#include "../../app/src/plugindatabasemanager.h"

LogcatTableModel::LogcatTableModel(QObject* parent)
    : QAbstractTableModel(parent)
    , m_databaseManager(nullptr)
    , m_databaseMode(false)
{
}

int LogcatTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    
    if (m_databaseMode) {
        return m_databaseResults.size();
    } else {
        return m_visibleRows.size();
    }
}

int LogcatTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 7; // No., Time, PID, TID, Level, Tag, Message
}

QVariant LogcatTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    LogcatEntry entry{LogEntry{}};
    int lineNumber = 0;
    
    if (m_databaseMode) {
        if (index.row() >= m_databaseResults.size())
            return QVariant();
        
        lineNumber = m_databaseLineNumbers[index.row()];
        entry = createEntryFromDatabaseRecord(m_databaseResults[index.row()], lineNumber);
    } else {
        if (index.row() >= m_visibleRows.size())
            return QVariant();
        
        int sourceRow = mapToSourceRow(index.row());
        if (sourceRow >= m_entries.size())
            return QVariant();
        
        lineNumber = sourceRow;
        entry = LogcatEntry(m_entries[sourceRow]);
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case LogcatColumn::No:
            return lineNumber + 1; // Line number (1-based)
        case LogcatColumn::Time:
            return entry.timestamp;
        case LogcatColumn::Pid:
            return entry.pid;
        case LogcatColumn::Tid:
            return entry.tid;
        case LogcatColumn::Level:
            return LogcatEntry::levelToString(entry.level);
        case LogcatColumn::Tag:
            return entry.tag;
        case LogcatColumn::Message:
            return entry.message;
        }
    }
    else if (role == Qt::BackgroundRole) {
        return LogcatEntry::levelColor(entry.level);
    }
    else if (role == Qt::ForegroundRole) {
        return QColor(Qt::black);
    }
    else if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case LogcatColumn::No:
        case LogcatColumn::Pid:
        case LogcatColumn::Tid:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

QVariant LogcatTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case LogcatColumn::No: return tr("No.");
    case LogcatColumn::Time: return tr("Time");
    case LogcatColumn::Pid: return tr("PID");
    case LogcatColumn::Tid: return tr("TID");
    case LogcatColumn::Level: return tr("Level");
    case LogcatColumn::Tag: return tr("Tag");
    case LogcatColumn::Message: return tr("Message");
    default: return QVariant();
    }
}

void LogcatTableModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    
    m_sortColumn = column;
    m_sortOrder = order;
    
    if (m_databaseMode) {
        sortDatabaseResults();
    } else {
        std::sort(m_visibleRows.begin(), m_visibleRows.end(), 
            [this, column, order](int left, int right) {
                const LogcatEntry& leftEntry(m_entries[left]);
                const LogcatEntry& rightEntry(m_entries[right]);
                
                bool lessThan;
                switch (column) {
                case LogcatColumn::No: // Line number
                    lessThan = left < right;
                    break;
                case LogcatColumn::Time:
                    lessThan = leftEntry.timestamp < rightEntry.timestamp;
                    break;
                case LogcatColumn::Pid:
                    {
                        bool leftOk = false, rightOk = false;
                        qint64 leftPid = 0, rightPid = 0;
                        
                        if (!leftEntry.pid.isEmpty()) {
                            leftPid = leftEntry.pid.toLongLong(&leftOk);
                        }
                        if (!rightEntry.pid.isEmpty()) {
                            rightPid = rightEntry.pid.toLongLong(&rightOk);
                        }
                        
                        if (leftOk == rightOk) {
                            lessThan = leftOk ? leftPid < rightPid : leftEntry.pid < rightEntry.pid;
                        } else {
                            lessThan = leftOk < rightOk; // Invalid values sort before valid ones
                        }
                    }
                    break;
                case LogcatColumn::Tid:
                    {
                        bool leftOk = false, rightOk = false;
                        qint64 leftTid = 0, rightTid = 0;
                        
                        if (!leftEntry.tid.isEmpty()) {
                            leftTid = leftEntry.tid.toLongLong(&leftOk);
                        }
                        if (!rightEntry.tid.isEmpty()) {
                            rightTid = rightEntry.tid.toLongLong(&rightOk);
                        }
                        
                        if (leftOk == rightOk) {
                            lessThan = leftOk ? leftTid < rightTid : leftEntry.tid < rightEntry.tid;
                        } else {
                            lessThan = leftOk < rightOk; // Invalid values sort before valid ones
                        }
                    }
                    break;
                case LogcatColumn::Level:
                    lessThan = leftEntry.level < rightEntry.level;
                    break;
                case LogcatColumn::Tag:
                    lessThan = leftEntry.tag < rightEntry.tag;
                    break;
                case LogcatColumn::Message:
                    lessThan = leftEntry.message < rightEntry.message;
                    break;
                default:
                    lessThan = false;
                }
                return lessThan;
            });
        
        // Apply sort order
        if (order == Qt::DescendingOrder) {
            std::reverse(m_visibleRows.begin(), m_visibleRows.end());
        }
    }
    
    endResetModel();
}

void LogcatTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    m_visibleRows.resize(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        m_visibleRows[i] = i;
    }
    endResetModel();
}

void LogcatTableModel::setVisibleRows(const QList<int>& linesToShow)
{
    if (m_databaseMode) {
        // In database mode, visible rows are managed differently
        return;
    }
    
    beginResetModel();
    m_visibleRows = linesToShow;
    sort(m_sortColumn, m_sortOrder);
    endResetModel();
}

void LogcatTableModel::setDatabaseManager(PluginDatabaseManager* manager)
{
    m_databaseManager = manager;
}

void LogcatTableModel::setDatabaseMode(bool enabled)
{
    if (m_databaseMode != enabled) {
        beginResetModel();
        m_databaseMode = enabled;
        
        if (enabled) {
            // Clear in-memory data when switching to database mode
            m_entries.clear();
            m_visibleRows.clear();
        } else {
            // Clear database results when switching to in-memory mode
            m_databaseResults.clear();
            m_databaseLineNumbers.clear();
        }
        
        endResetModel();
    }
}

void LogcatTableModel::setDatabaseFilter(const FilterOptions& filter)
{
    if (!m_databaseMode || !m_databaseManager) {
        return;
    }
    
    m_currentFilter = filter;
    loadDataFromDatabase();
}

void LogcatTableModel::refreshFromDatabase()
{
    if (m_databaseMode && m_databaseManager) {
        loadDataFromDatabase();
    }
}

LogcatEntry LogcatTableModel::createEntryFromDatabaseRecord(const QVariantList& record, int lineNumber) const
{
    Q_UNUSED(lineNumber);
    
    // Create a dummy LogEntry to construct LogcatEntry
    LogEntry dummyEntry;
    LogcatEntry entry(dummyEntry);
    
    // Manually set the fields from database record
    // Database schema: timestamp, pid, tid, level, tag, message
    if (record.size() >= 6) {
        entry.timestamp = record[0].toString();
        entry.pid = record[1].toString();
        entry.tid = record[2].toString();
        
        // Convert level string back to enum
        QString levelStr = record[3].toString();
        if (levelStr == "Verbose") entry.level = LogcatEntry::Level::Verbose;
        else if (levelStr == "Debug") entry.level = LogcatEntry::Level::Debug;
        else if (levelStr == "Info") entry.level = LogcatEntry::Level::Info;
        else if (levelStr == "Warning") entry.level = LogcatEntry::Level::Warning;
        else if (levelStr == "Error") entry.level = LogcatEntry::Level::Error;
        else if (levelStr == "Fatal") entry.level = LogcatEntry::Level::Fatal;
        else entry.level = LogcatEntry::Level::Unknown;
        
        entry.tag = record[4].toString();
        entry.message = record[5].toString();
    }
    
    return entry;
}

void LogcatTableModel::loadDataFromDatabase()
{
    if (!m_databaseManager || !m_databaseManager->isReady()) {
        return;
    }
    
    beginResetModel();
    
    // Query data from database
    m_databaseResults = m_databaseManager->queryData("logcatviewer", m_currentFilter);
    
    // Get corresponding line numbers
    QSet<int> lineNumbersSet = m_databaseManager->getFilteredLineNumbers("logcatviewer", m_currentFilter);
    m_databaseLineNumbers = lineNumbersSet.values();
    
    // Ensure we have matching counts
    if (m_databaseResults.size() != m_databaseLineNumbers.size()) {
        qWarning() << "Mismatch between database results and line numbers";
        // Try to fix by limiting to smaller size
        int minSize = qMin(m_databaseResults.size(), m_databaseLineNumbers.size());
        m_databaseResults = m_databaseResults.mid(0, minSize);
        m_databaseLineNumbers = m_databaseLineNumbers.mid(0, minSize);
    }
    
    // Sort the results
    sortDatabaseResults();
    
    endResetModel();
}

void LogcatTableModel::sortDatabaseResults()
{
    if (m_databaseResults.isEmpty()) {
        return;
    }
    
    // Create index pairs for sorting
    QList<QPair<int, int>> indexPairs;
    for (int i = 0; i < m_databaseResults.size(); ++i) {
        indexPairs.append(qMakePair(i, m_databaseLineNumbers[i]));
    }
    
    // Sort based on current sort column and order
    std::sort(indexPairs.begin(), indexPairs.end(), 
        [this](const QPair<int, int>& left, const QPair<int, int>& right) {
            const QVariantList& leftRecord = m_databaseResults[left.first];
            const QVariantList& rightRecord = m_databaseResults[right.first];
            
            bool lessThan;
            switch (m_sortColumn) {
            case LogcatColumn::No: // Line number
                lessThan = left.second < right.second;
                break;
            case LogcatColumn::Time:
                lessThan = leftRecord[0].toString() < rightRecord[0].toString();
                break;
            case LogcatColumn::Pid:
                {
                    QString leftPid = leftRecord[1].toString();
                    QString rightPid = rightRecord[1].toString();
                    bool leftOk = false, rightOk = false;
                    qint64 leftPidNum = leftPid.toLongLong(&leftOk);
                    qint64 rightPidNum = rightPid.toLongLong(&rightOk);
                    
                    if (leftOk == rightOk) {
                        lessThan = leftOk ? leftPidNum < rightPidNum : leftPid < rightPid;
                    } else {
                        lessThan = leftOk < rightOk;
                    }
                }
                break;
            case LogcatColumn::Tid:
                {
                    QString leftTid = leftRecord[2].toString();
                    QString rightTid = rightRecord[2].toString();
                    bool leftOk = false, rightOk = false;
                    qint64 leftTidNum = leftTid.toLongLong(&leftOk);
                    qint64 rightTidNum = rightTid.toLongLong(&rightOk);
                    
                    if (leftOk == rightOk) {
                        lessThan = leftOk ? leftTidNum < rightTidNum : leftTid < rightTid;
                    } else {
                        lessThan = leftOk < rightOk;
                    }
                }
                break;
            case LogcatColumn::Level:
                lessThan = leftRecord[3].toString() < rightRecord[3].toString();
                break;
            case LogcatColumn::Tag:
                lessThan = leftRecord[4].toString() < rightRecord[4].toString();
                break;
            case LogcatColumn::Message:
                lessThan = leftRecord[5].toString() < rightRecord[5].toString();
                break;
            default:
                lessThan = false;
            }
            
            return m_sortOrder == Qt::AscendingOrder ? lessThan : !lessThan;
        });
    
    // Reorder the results based on sorted indices
    QList<QVariantList> sortedResults;
    QList<int> sortedLineNumbers;
    
    for (const auto& pair : indexPairs) {
        sortedResults.append(m_databaseResults[pair.first]);
        sortedLineNumbers.append(pair.second);
    }
    
    m_databaseResults = sortedResults;
    m_databaseLineNumbers = sortedLineNumbers;
}
