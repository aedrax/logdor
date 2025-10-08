#include "regextablemodel.h"
#include <QDebug>
#include <QSqlError>
#include <QTimer>
#include <QRegularExpression>
#include <QDateTime>

RegexTableModel::RegexTableModel(DatabaseManager* database, QObject* parent)
    : QAbstractTableModel(parent)
    , m_database(database)
    , m_cacheStartRow(0)
    , m_cacheSize(0)
    , m_lazyLoadingEnabled(true)
    , m_batchSize(DEFAULT_BATCH_SIZE)
    , m_loadedRowCount(0)
    , m_totalRowCount(0)
    , m_hasActiveFilter(false)
    , m_hasActiveSearch(false)
    , m_mappingValid(false)
    , m_refreshPending(false)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(100); // 100ms delay for refresh batching
    
    connect(m_refreshTimer, &QTimer::timeout, this, &RegexTableModel::performDelayedRefresh);
    
    if (m_database) {
        connect(m_database, &DatabaseManager::statusChanged, 
                this, &RegexTableModel::onDatabaseStatusChanged);
        connect(m_database, &DatabaseManager::errorOccurred,
                this, &RegexTableModel::onDatabaseError);
    }
}

RegexTableModel::~RegexTableModel()
{
    clearCache();
}

int RegexTableModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    
    if (m_lazyLoadingEnabled) {
        return m_loadedRowCount;
    } else {
        return m_totalRowCount;
    }
}

int RegexTableModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return m_columns.size();
}

QVariant RegexTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= columnCount()) {
        return QVariant();
    }
    
    const int row = index.row();
    const int column = index.column();
    
    // Check if we have this row in cache
    if (!m_rowCache.contains(row)) {
        // Trigger lazy loading if needed
        if (m_lazyLoadingEnabled && row >= m_loadedRowCount - m_batchSize / 2) {
            const_cast<RegexTableModel*>(this)->fetchMore(QModelIndex());
        }
        return QVariant(); // Return empty while loading
    }
    
    const QVariantList& rowData = m_rowCache[row];
    if (column >= rowData.size()) {
        return QVariant();
    }
    
    const QVariant& value = rowData[column];
    const DataType dataType = (column < m_columns.size()) ? m_columns[column].type : DataType::String;
    
    switch (role) {
    case Qt::DisplayRole:
        return formatDisplayData(value, dataType);
        
    case Qt::UserRole:
        return formatUserRole(value, dataType);
        
    case Qt::TextAlignmentRole:
        return QVariant::fromValue(getColumnAlignment(column));
        
    default:
        return QVariant();
    }
}

QVariant RegexTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    
    if (orientation == Qt::Horizontal && section < m_columns.size()) {
        return m_columns[section].displayName;
    }
    
    if (orientation == Qt::Vertical) {
        return section + 1; // 1-based row numbers
    }
    
    return QVariant();
}

bool RegexTableModel::canFetchMore(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return m_lazyLoadingEnabled && m_loadedRowCount < m_totalRowCount;
}

void RegexTableModel::fetchMore(const QModelIndex& parent)
{
    Q_UNUSED(parent);
    
    if (!m_lazyLoadingEnabled || m_loadedRowCount >= m_totalRowCount) {
        return;
    }
    
    const int rowsToFetch = qMin(m_batchSize, m_totalRowCount - m_loadedRowCount);
    if (rowsToFetch <= 0) {
        return;
    }
    
    beginInsertRows(QModelIndex(), m_loadedRowCount, m_loadedRowCount + rowsToFetch - 1);
    
    if (loadData(m_loadedRowCount, rowsToFetch)) {
        m_loadedRowCount += rowsToFetch;
    }
    
    endInsertRows();
}

void RegexTableModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= m_columns.size()) {
        return;
    }
    
    if (!m_columns[column].sortable) {
        return;
    }
    
    // Update sort order
    m_sortOrders.clear();
    SortOrder sortOrder;
    sortOrder.column = column;
    sortOrder.order = order;
    m_sortOrders.append(sortOrder);
    
    // Refresh data with new sort order
    refresh();
}

void RegexTableModel::setColumnDefinitions(const QList<ColumnDefinition>& columns)
{
    beginResetModel();
    m_columns = columns;
    clearCache();
    m_mappingValid = false;
    endResetModel();
}

void RegexTableModel::setTableName(const QString& tableName)
{
    if (m_tableName != tableName) {
        beginResetModel();
        m_tableName = tableName;
        clearCache();
        m_mappingValid = false;
        endResetModel();
    }
}

void RegexTableModel::refresh()
{
    if (m_refreshTimer->isActive()) {
        return; // Already pending
    }
    
    m_refreshPending = true;
    m_refreshTimer->start();
}

void RegexTableModel::clear()
{
    beginResetModel();
    clearCache();
    m_loadedRowCount = 0;
    m_totalRowCount = 0;
    m_mappingValid = false;
    endResetModel();
}

void RegexTableModel::applyFilter(const FilterOptions& options)
{
    m_currentFilter = options;
    m_hasActiveFilter = !options.query.isEmpty();
    m_hasActiveSearch = false; // Filter overrides search
    refresh();
    emit filterApplied();
}

void RegexTableModel::performSearch(const SearchQuery& query)
{
    m_currentSearch = query;
    m_hasActiveSearch = !query.query.isEmpty();
    m_hasActiveFilter = false; // Search overrides filter
    
    if (m_hasActiveSearch) {
        loadSearchResults();
    } else {
        refresh();
    }
    
    emit searchCompleted(m_hasActiveSearch ? m_searchResults.size() : m_totalRowCount);
}

void RegexTableModel::clearFilter()
{
    m_hasActiveFilter = false;
    m_currentFilter = FilterOptions();
    refresh();
}

void RegexTableModel::clearSearch()
{
    m_hasActiveSearch = false;
    m_currentSearch = SearchQuery();
    m_searchResults.clear();
    refresh();
}

int RegexTableModel::mapToSourceRow(int modelRow) const
{
    if (!m_mappingValid || modelRow < 0 || modelRow >= m_modelToSourceMap.size()) {
        return modelRow; // 1:1 mapping when no filtering
    }
    
    return m_modelToSourceMap[modelRow];
}

int RegexTableModel::mapFromSourceRow(int sourceRow) const
{
    if (!m_mappingValid) {
        return sourceRow; // 1:1 mapping when no filtering
    }
    
    return m_sourceToModelMap.value(sourceRow, -1);
}

QList<int> RegexTableModel::getSourceRowsForModelRows(const QList<int>& modelRows) const
{
    QList<int> sourceRows;
    for (int modelRow : modelRows) {
        int sourceRow = mapToSourceRow(modelRow);
        if (sourceRow >= 0) {
            sourceRows.append(sourceRow);
        }
    }
    return sourceRows;
}

QVariant RegexTableModel::getFieldValue(int row, const QString& fieldName) const
{
    if (row < 0 || row >= rowCount()) {
        return QVariant();
    }
    
    // Find column index for field name
    int columnIndex = -1;
    for (int i = 0; i < m_columns.size(); ++i) {
        if (m_columns[i].name == fieldName) {
            columnIndex = i;
            break;
        }
    }
    
    if (columnIndex < 0) {
        return QVariant();
    }
    
    return data(index(row, columnIndex), Qt::UserRole);
}

QVariantMap RegexTableModel::getRowData(int row) const
{
    QVariantMap rowData;
    
    if (row < 0 || row >= rowCount()) {
        return rowData;
    }
    
    for (int col = 0; col < m_columns.size(); ++col) {
        QVariant value = data(index(row, col), Qt::UserRole);
        rowData[m_columns[col].name] = value;
    }
    
    return rowData;
}

QStringList RegexTableModel::getFieldNames() const
{
    QStringList names;
    for (const ColumnDefinition& column : m_columns) {
        names.append(column.name);
    }
    return names;
}

void RegexTableModel::setSelectedSourceRows(const QList<int>& rows)
{
    m_selectedSourceRows = rows;
}

void RegexTableModel::onDatabaseStatusChanged(DatabaseStatus status)
{
    if (status == DatabaseStatus::Ready) {
        refresh();
    } else if (status == DatabaseStatus::Error) {
        emit errorOccurred("Database error occurred");
    }
}

void RegexTableModel::onDatabaseError(const QString& error)
{
    emit errorOccurred(error);
}

void RegexTableModel::performDelayedRefresh()
{
    if (!m_refreshPending) {
        return;
    }
    
    m_refreshPending = false;
    
    beginResetModel();
    
    emit loadingStarted();
    
    clearCache();
    m_loadedRowCount = 0;
    
    // Get total row count first
    QString countQuery = QString("SELECT COUNT(*) FROM %1").arg(m_tableName);
    if (m_hasActiveFilter) {
        QString filterCondition = buildFilterCondition();
        if (!filterCondition.isEmpty()) {
            countQuery += " WHERE " + filterCondition;
        }
    }
    
    QVariant totalCount = m_database->executeScalar(countQuery);
    m_totalRowCount = totalCount.toInt();
    
    // Load initial batch if not using search
    if (!m_hasActiveSearch) {
        if (m_lazyLoadingEnabled) {
            int initialLoad = qMin(m_batchSize, m_totalRowCount);
            if (loadData(0, initialLoad)) {
                m_loadedRowCount = initialLoad;
            }
        } else {
            if (loadData()) {
                m_loadedRowCount = m_totalRowCount;
            }
        }
    }
    
    endResetModel();
    
    emit loadingFinished();
    emit dataRefreshed();
}

bool RegexTableModel::loadData(int offset, int limit)
{
    if (!m_database || m_tableName.isEmpty() || m_columns.isEmpty()) {
        return false;
    }
    
    QString query = buildQuery(offset, limit);
    QSqlQuery sqlQuery = m_database->executeQuery(query);
    
    if (sqlQuery.lastError().isValid()) {
        emit errorOccurred(sqlQuery.lastError().text());
        return false;
    }
    
    cacheRowData(offset, sqlQuery);
    return true;
}

bool RegexTableModel::loadSearchResults()
{
    if (!m_database || !m_hasActiveSearch) {
        return false;
    }
    
    // Perform search using DatabaseManager
    m_searchResults = m_database->performSearch(m_currentSearch, m_tableName);
    
    beginResetModel();
    clearCache();
    
    // Load data for search results
    QStringList entryIds;
    for (const SearchResult& result : m_searchResults) {
        entryIds.append(QString::number(result.entryId));
    }
    
    if (!entryIds.isEmpty()) {
        QString query = QString("SELECT * FROM %1 WHERE id IN (%2)")
                           .arg(m_tableName, entryIds.join(","));
        
        QString sortCondition = buildSortCondition();
        if (!sortCondition.isEmpty()) {
            query += " ORDER BY " + sortCondition;
        }
        
        QSqlQuery sqlQuery = m_database->executeQuery(query);
        if (!sqlQuery.lastError().isValid()) {
            cacheRowData(0, sqlQuery);
            m_loadedRowCount = m_searchResults.size();
            m_totalRowCount = m_searchResults.size();
        }
    } else {
        m_loadedRowCount = 0;
        m_totalRowCount = 0;
    }
    
    endResetModel();
    return true;
}

QString RegexTableModel::buildQuery(int offset, int limit) const
{
    QStringList columnNames;
    for (const ColumnDefinition& column : m_columns) {
        columnNames.append(escapeFieldName(column.name));
    }
    
    QString query = QString("SELECT %1 FROM %2")
                       .arg(columnNames.join(", "), m_tableName);
    
    // Add filter condition
    if (m_hasActiveFilter) {
        QString filterCondition = buildFilterCondition();
        if (!filterCondition.isEmpty()) {
            query += " WHERE " + filterCondition;
        }
    }
    
    // Add sort condition
    QString sortCondition = buildSortCondition();
    if (!sortCondition.isEmpty()) {
        query += " ORDER BY " + sortCondition;
    }
    
    // Add limit and offset
    if (limit > 0) {
        query += QString(" LIMIT %1").arg(limit);
    }
    if (offset > 0) {
        query += QString(" OFFSET %1").arg(offset);
    }
    
    return query;
}

QString RegexTableModel::buildFilterCondition() const
{
    if (!m_hasActiveFilter || m_currentFilter.query.isEmpty()) {
        return QString();
    }
    
    QStringList conditions;
    
    for (const ColumnDefinition& column : m_columns) {
        if (!column.filterable) {
            continue;
        }
        
        QString condition;
        if (m_currentFilter.inRegexMode) {
            condition = buildRegexCondition(column.name, m_currentFilter.query, m_currentFilter.caseSensitivity);
        } else {
            condition = buildTextSearchCondition(column.name, m_currentFilter.query, m_currentFilter.caseSensitivity);
        }
        
        if (!condition.isEmpty()) {
            conditions.append(condition);
        }
    }
    
    if (conditions.isEmpty()) {
        return QString();
    }
    
    QString result = "(" + conditions.join(" OR ") + ")";
    
    if (m_currentFilter.invertFilter) {
        result = "NOT " + result;
    }
    
    return result;
}

QString RegexTableModel::buildSortCondition() const
{
    QStringList sortParts;
    
    for (const SortOrder& sortOrder : m_sortOrders) {
        if (sortOrder.column >= 0 && sortOrder.column < m_columns.size()) {
            QString columnName = escapeFieldName(m_columns[sortOrder.column].name);
            QString orderStr = (sortOrder.order == Qt::AscendingOrder) ? "ASC" : "DESC";
            sortParts.append(columnName + " " + orderStr);
        }
    }
    
    return sortParts.join(", ");
}

void RegexTableModel::cacheRowData(int startRow, const QSqlQuery& query)
{
    QSqlQuery q = query; // Make a copy to modify
    int row = startRow;
    
    while (q.next()) {
        QVariantList rowData;
        for (int col = 0; col < m_columns.size(); ++col) {
            rowData.append(q.value(col));
        }
        m_rowCache[row] = rowData;
        ++row;
    }
    
    // Update cache metadata
    if (row > startRow) {
        m_cacheStartRow = startRow;
        m_cacheSize = row - startRow;
        
        // Limit cache size
        if (m_rowCache.size() > MAX_CACHE_SIZE) {
            // Remove oldest entries (simple strategy)
            auto it = m_rowCache.begin();
            int toRemove = m_rowCache.size() - MAX_CACHE_SIZE / 2;
            while (it != m_rowCache.end() && toRemove > 0) {
                if (it.key() < startRow - MAX_CACHE_SIZE / 4) {
                    it = m_rowCache.erase(it);
                    --toRemove;
                } else {
                    ++it;
                }
            }
        }
    }
}

void RegexTableModel::clearCache()
{
    m_rowCache.clear();
    m_cacheStartRow = 0;
    m_cacheSize = 0;
    m_modelToSourceMap.clear();
    m_sourceToModelMap.clear();
    m_mappingValid = false;
}

QString RegexTableModel::escapeFieldName(const QString& fieldName) const
{
    return QString("\"%1\"").arg(fieldName);
}

QString RegexTableModel::buildRegexCondition(const QString& field, const QString& pattern, Qt::CaseSensitivity cs) const
{
    QString escapedField = escapeFieldName(field);
    QString flags = (cs == Qt::CaseInsensitive) ? "i" : "";
    return QString("%1 REGEXP '%2' %3").arg(escapedField, m_database->escapeString(pattern), flags);
}

QString RegexTableModel::buildTextSearchCondition(const QString& field, const QString& text, Qt::CaseSensitivity cs) const
{
    QString escapedField = escapeFieldName(field);
    QString searchTerm = "%" + m_database->escapeString(text) + "%";
    
    if (cs == Qt::CaseInsensitive) {
        return QString("LOWER(%1) LIKE LOWER('%2')").arg(escapedField, searchTerm);
    } else {
        return QString("%1 LIKE '%2'").arg(escapedField, searchTerm);
    }
}

QVariant RegexTableModel::formatDisplayData(const QVariant& value, DataType type) const
{
    if (value.isNull()) {
        return QString("-");
    }
    
    switch (type) {
    case DataType::DateTime:
        if (value.metaType() == QMetaType::fromType<QDateTime>()) {
            return value.toDateTime().toString("yyyy-MM-dd hh:mm:ss");
        }
        break;
    case DataType::Integer:
        return value.toString();
    default:
        break;
    }
    
    return value.toString();
}

QVariant RegexTableModel::formatUserRole(const QVariant& value, DataType type) const
{
    Q_UNUSED(type);
    return value; // Return raw value for UserRole
}

Qt::Alignment RegexTableModel::getColumnAlignment(int column) const
{
    if (column >= 0 && column < m_columns.size()) {
        return m_columns[column].alignment;
    }
    return Qt::AlignLeft | Qt::AlignVCenter;
}

void RegexTableModel::startBackgroundLoad()
{
    // Placeholder for future background loading implementation
}

void RegexTableModel::stopBackgroundLoad()
{
    // Placeholder for future background loading implementation
}
