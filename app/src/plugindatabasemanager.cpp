#include "plugindatabasemanager.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSqlRecord>

QString DatabaseFieldInfo::generateSqlType() const
{
    switch (type) {
        case DataType::String:
            return "TEXT";
        case DataType::Integer:
            return "INTEGER";
        case DataType::DateTime:
            return "TIMESTAMP";
        default:
            return "TEXT";
    }
}

// PluginQueryBuilder implementation
PluginQueryBuilder::PluginQueryBuilder()
    : m_orderDirection(Qt::AscendingOrder)
    , m_limitCount(-1)
    , m_offsetCount(-1)
{
}

PluginQueryBuilder& PluginQueryBuilder::where(const QString& field, const QVariant& value)
{
    QueryCondition condition;
    condition.type = QueryCondition::Where;
    condition.field = field;
    condition.value = value;
    m_conditions.append(condition);
    addParameter(value);
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::whereLike(const QString& field, const QString& pattern, Qt::CaseSensitivity caseSensitivity)
{
    QueryCondition condition;
    condition.type = QueryCondition::WhereLike;
    condition.field = field;
    condition.value = pattern;
    condition.caseSensitivity = caseSensitivity;
    m_conditions.append(condition);
    addParameter(pattern);
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::whereRegex(const QString& field, const QString& pattern, Qt::CaseSensitivity caseSensitivity)
{
    QueryCondition condition;
    condition.type = QueryCondition::WhereRegex;
    condition.field = field;
    condition.value = pattern;
    condition.caseSensitivity = caseSensitivity;
    m_conditions.append(condition);
    addParameter(pattern);
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::whereNot(const QString& field, const QVariant& value)
{
    QueryCondition condition;
    condition.type = QueryCondition::WhereNot;
    condition.field = field;
    condition.value = value;
    m_conditions.append(condition);
    addParameter(value);
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::andCondition()
{
    QueryCondition condition;
    condition.type = QueryCondition::And;
    m_conditions.append(condition);
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::orCondition()
{
    QueryCondition condition;
    condition.type = QueryCondition::Or;
    m_conditions.append(condition);
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::openGroup()
{
    QueryCondition condition;
    condition.type = QueryCondition::OpenGroup;
    m_conditions.append(condition);
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::closeGroup()
{
    QueryCondition condition;
    condition.type = QueryCondition::CloseGroup;
    m_conditions.append(condition);
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::orderBy(const QString& field, Qt::SortOrder order)
{
    m_orderByField = field;
    m_orderDirection = order;
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::limit(int count)
{
    m_limitCount = count;
    return *this;
}

PluginQueryBuilder& PluginQueryBuilder::offset(int count)
{
    m_offsetCount = count;
    return *this;
}

QString PluginQueryBuilder::buildQuery(const QString& tableName, const QStringList& selectFields) const
{
    QString sql;
    
    // SELECT clause
    if (selectFields.isEmpty()) {
        sql = QString("SELECT * FROM %1").arg(tableName);
    } else {
        sql = QString("SELECT %1 FROM %2").arg(selectFields.join(", "), tableName);
    }
    
    // WHERE clause
    QString whereClause = buildWhereClause();
    if (!whereClause.isEmpty()) {
        sql += " WHERE " + whereClause;
    }
    
    // ORDER BY clause
    if (!m_orderByField.isEmpty()) {
        sql += QString(" ORDER BY %1 %2")
               .arg(m_orderByField)
               .arg(m_orderDirection == Qt::AscendingOrder ? "ASC" : "DESC");
    }
    
    // LIMIT clause
    if (m_limitCount > 0) {
        sql += QString(" LIMIT %1").arg(m_limitCount);
    }
    
    // OFFSET clause
    if (m_offsetCount > 0) {
        sql += QString(" OFFSET %1").arg(m_offsetCount);
    }
    
    return sql;
}

QString PluginQueryBuilder::buildCountQuery(const QString& tableName) const
{
    QString sql = QString("SELECT COUNT(*) FROM %1").arg(tableName);
    
    QString whereClause = buildWhereClause();
    if (!whereClause.isEmpty()) {
        sql += " WHERE " + whereClause;
    }
    
    return sql;
}

QVariantList PluginQueryBuilder::getParameters() const
{
    return m_parameters;
}

void PluginQueryBuilder::reset()
{
    m_conditions.clear();
    m_parameters.clear();
    m_orderByField.clear();
    m_orderDirection = Qt::AscendingOrder;
    m_limitCount = -1;
    m_offsetCount = -1;
}

PluginQueryBuilder PluginQueryBuilder::fromFilterOptions(const FilterOptions& filter, const QList<FieldInfo>& schema)
{
    PluginQueryBuilder builder;
    
    if (filter.query.isEmpty()) {
        return builder;
    }
    
    // Find all string fields for text search
    QStringList textFields;
    for (const FieldInfo& field : schema) {
        if (field.type == DataType::String) {
            textFields.append(field.name);
        }
    }
    
    if (textFields.isEmpty()) {
        return builder;
    }
    
    // Build query based on filter mode
    if (filter.inRegexMode) {
        // Regex mode - search across all text fields
        builder.openGroup();
        for (int i = 0; i < textFields.size(); ++i) {
            if (i > 0) {
                builder.orCondition();
            }
            if (filter.invertFilter) {
                // For inverted regex, we need to use NOT REGEXP
                // SQLite doesn't have built-in REGEXP, so we'll use a workaround
                builder.whereNot(textFields[i], filter.query);
            } else {
                builder.whereRegex(textFields[i], filter.query, filter.caseSensitivity);
            }
        }
        builder.closeGroup();
    } else {
        // Text search mode - use LIKE
        builder.openGroup();
        for (int i = 0; i < textFields.size(); ++i) {
            if (i > 0) {
                builder.orCondition();
            }
            QString pattern = QString("%%1%").arg(filter.query);
            if (filter.invertFilter) {
                // For inverted filter, we need to exclude matches
                builder.whereNot(textFields[i], pattern);
            } else {
                builder.whereLike(textFields[i], pattern, filter.caseSensitivity);
            }
        }
        builder.closeGroup();
    }
    
    // Always order by original line number for consistent results
    builder.orderBy("original_line_number", Qt::AscendingOrder);
    
    return builder;
}

QString PluginQueryBuilder::buildWhereClause() const
{
    if (m_conditions.isEmpty()) {
        return QString();
    }
    
    QStringList parts;
    
    for (const QueryCondition& condition : m_conditions) {
        switch (condition.type) {
            case QueryCondition::Where:
                parts.append(QString("%1 = ?").arg(condition.field));
                break;
                
            case QueryCondition::WhereLike:
                if (condition.caseSensitivity == Qt::CaseInsensitive) {
                    parts.append(QString("LOWER(%1) LIKE LOWER(?)").arg(condition.field));
                } else {
                    parts.append(QString("%1 LIKE ?").arg(condition.field));
                }
                break;
                
            case QueryCondition::WhereRegex:
                // SQLite doesn't have built-in REGEX, so we'll use GLOB for basic pattern matching
                // For more complex regex, we'd need to enable REGEXP extension
                if (condition.caseSensitivity == Qt::CaseInsensitive) {
                    parts.append(QString("LOWER(%1) GLOB LOWER(?)").arg(condition.field));
                } else {
                    parts.append(QString("%1 GLOB ?").arg(condition.field));
                }
                break;
                
            case QueryCondition::WhereNot:
                parts.append(QString("%1 != ?").arg(condition.field));
                break;
                
            case QueryCondition::And:
                parts.append("AND");
                break;
                
            case QueryCondition::Or:
                parts.append("OR");
                break;
                
            case QueryCondition::OpenGroup:
                parts.append("(");
                break;
                
            case QueryCondition::CloseGroup:
                parts.append(")");
                break;
        }
    }
    
    return parts.join(" ");
}

void PluginQueryBuilder::addParameter(const QVariant& param)
{
    m_parameters.append(param);
}

PluginDatabaseManager::PluginDatabaseManager(QObject* parent)
    : QObject(parent)
    , m_status(PluginDatabaseStatus::NotInitialized)
    , m_dbManager(nullptr)
    , m_fallbackMode(false)
    , m_consecutiveErrors(0)
{
}

PluginDatabaseManager::~PluginDatabaseManager()
{
    closeDatabase();
}

bool PluginDatabaseManager::initializeForFile(const QString& filePath)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_currentFilePath == filePath && m_status == PluginDatabaseStatus::Ready) {
        return true; // Already initialized for this file
    }
    
    setStatus(PluginDatabaseStatus::Initializing);
    
    // Close existing database if open
    if (m_dbManager) {
        m_dbManager->closeDatabase();
        delete m_dbManager;
        m_dbManager = nullptr;
    }
    
    // Generate database path for this file
    m_databasePath = generateDatabasePath(filePath);
    m_currentFilePath = filePath;
    
    // Create new database manager
    m_dbManager = new DatabaseManager(m_databasePath, this);
    
    // Connect signals
    connect(m_dbManager, &DatabaseManager::errorOccurred, 
            this, &PluginDatabaseManager::handleDatabaseError);
    connect(m_dbManager, &DatabaseManager::statusChanged,
            this, &PluginDatabaseManager::handleDatabaseStatusChanged);
    
    // Initialize database with base schema
    QString baseSchema = R"(
        CREATE TABLE IF NOT EXISTS plugin_metadata (
            plugin_name TEXT PRIMARY KEY,
            schema_version INTEGER NOT NULL DEFAULT 1,
            field_definitions TEXT NOT NULL,
            last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS file_metadata (
            file_path TEXT PRIMARY KEY,
            file_size INTEGER,
            file_modified TIMESTAMP,
            database_version INTEGER DEFAULT 1,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    
    if (!m_dbManager->createDatabase(baseSchema)) {
        setError(QString("Failed to initialize database: %1").arg(m_dbManager->lastError()));
        return false;
    }
    
    if (!createMetadataTables()) {
        setError("Failed to create metadata tables");
        return false;
    }
    
    // Record file metadata for tracking
    recordFileMetadata(filePath);
    
    // Check database integrity on initialization
    if (!checkDatabaseIntegrity()) {
        qWarning() << "Database integrity check failed during initialization";
        
        // Attempt to repair the database
        if (repairDatabase()) {
            qDebug() << "Database repair successful";
        } else {
            qWarning() << "Database repair failed, will attempt rebuild if needed";
            // Don't fail initialization here - let plugins handle fallback
            // The database will be marked as having issues but still usable
        }
    }
    
    setStatus(PluginDatabaseStatus::Ready);
    return true;
}

bool PluginDatabaseManager::createPluginTable(const QString& pluginName, const QList<FieldInfo>& schema)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    if (pluginName.isEmpty()) {
        setError("Plugin name cannot be empty");
        return false;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    // Check if table already exists
    if (m_dbManager->tableExists(tableName)) {
        // Verify schema matches
        QList<FieldInfo> existingSchema = getPluginSchema(pluginName);
        if (existingSchema.size() == schema.size()) {
            bool schemaMatches = true;
            for (int i = 0; i < schema.size(); ++i) {
                if (schema[i].name != existingSchema[i].name || 
                    schema[i].type != existingSchema[i].type) {
                    schemaMatches = false;
                    break;
                }
            }
            if (schemaMatches) {
                return true; // Table exists with correct schema
            }
        }
        
        // Schema mismatch - need to update
        return updatePluginSchema(pluginName, schema);
    }
    
    // Convert to database field info
    QList<DatabaseFieldInfo> dbFields = convertToDbFieldInfo(schema);
    
    // Generate CREATE TABLE SQL
    QString createSql = generateCreateTableSql(tableName, dbFields);
    
    if (!m_dbManager->executeNonQuery(createSql)) {
        setError(QString("Failed to create plugin table: %1").arg(m_dbManager->lastError()));
        return false;
    }
    
    // Create indexes
    if (!createIndexes(tableName, dbFields)) {
        setError("Failed to create indexes");
        return false;
    }
    
    // Save schema metadata
    if (!savePluginSchema(pluginName, schema)) {
        setError("Failed to save plugin schema metadata");
        return false;
    }
    
    // Cache schema
    m_schemaCache[pluginName] = schema;
    
    return true;
}

void PluginDatabaseManager::closeDatabase()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_dbManager) {
        m_dbManager->closeDatabase();
        delete m_dbManager;
        m_dbManager = nullptr;
    }
    
    m_schemaCache.clear();
    m_currentFilePath.clear();
    m_databasePath.clear();
    setStatus(PluginDatabaseStatus::NotInitialized);
}

bool PluginDatabaseManager::insertParsedData(const QString& pluginName, const QVariantList& data, int originalLineNumber)
{
    QMutexLocker locker(&m_mutex);
    
    // If in fallback mode, return false to trigger plugin fallback to in-memory storage
    if (m_fallbackMode) {
        return false;
    }
    
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    if (pluginName.isEmpty()) {
        setError("Plugin name cannot be empty");
        return false;
    }
    
    if (originalLineNumber < 0) {
        setError("Original line number must be non-negative");
        return false;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return false;
    }
    
    // Get schema to build proper insert and validate data
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return false;
    }
    
    // Validate data count matches schema
    if (data.size() != schema.size()) {
        setError(QString("Data field count (%1) does not match schema field count (%2)")
                .arg(data.size()).arg(schema.size()));
        return false;
    }
    
    // Validate and convert data types
    QVariantList validatedData;
    for (int i = 0; i < schema.size(); ++i) {
        const FieldInfo& field = schema[i];
        QVariant value = data[i];
        
        // Convert and validate data type
        QVariant convertedValue = convertToSqlType(value, field.type, field.name);
        if (!convertedValue.isValid() && !value.isNull()) {
            setError(QString("Failed to convert field '%1' to required type").arg(field.name));
            return false;
        }
        
        validatedData.append(convertedValue);
    }
    
    QList<DatabaseFieldInfo> dbFields = convertToDbFieldInfo(schema);
    QString insertSql = generateInsertSql(tableName, dbFields);
    
    // Prepare parameters: original_line_number + validated data
    QVariantList params;
    params.append(originalLineNumber);
    params.append(validatedData);
    
    if (!m_dbManager->executeNonQuery(insertSql, params)) {
        setError(QString("Failed to insert data: %1").arg(m_dbManager->lastError()));
        return false;
    }
    
    // Reset error counter on successful operation
    m_consecutiveErrors = 0;
    return true;
}

bool PluginDatabaseManager::insertBatchData(const QString& pluginName, const QList<QVariantList>& batchData, const QList<int>& lineNumbers)
{
    QMutexLocker locker(&m_mutex);
    
    // If in fallback mode, return false to trigger plugin fallback to in-memory storage
    if (m_fallbackMode) {
        return false;
    }
    
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    if (pluginName.isEmpty()) {
        setError("Plugin name cannot be empty");
        return false;
    }
    
    if (batchData.isEmpty()) {
        // Empty batch is valid, just return success
        return true;
    }
    
    if (batchData.size() != lineNumbers.size()) {
        setError(QString("Batch data size (%1) does not match line numbers size (%2)")
                .arg(batchData.size()).arg(lineNumbers.size()));
        return false;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return false;
    }
    
    // Get schema for validation
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return false;
    }
    
    // Validate all records before starting transaction
    QList<QVariantList> validatedBatchData;
    for (int i = 0; i < batchData.size(); ++i) {
        const QVariantList& data = batchData[i];
        int lineNumber = lineNumbers[i];
        
        // Validate line number
        if (lineNumber < 0) {
            setError(QString("Invalid line number %1 at batch index %2").arg(lineNumber).arg(i));
            return false;
        }
        
        // Validate data count matches schema
        if (data.size() != schema.size()) {
            setError(QString("Data field count (%1) does not match schema field count (%2) at batch index %3")
                    .arg(data.size()).arg(schema.size()).arg(i));
            return false;
        }
        
        // Validate and convert data types
        QVariantList validatedData;
        for (int j = 0; j < schema.size(); ++j) {
            const FieldInfo& field = schema[j];
            QVariant value = data[j];
            
            // Convert and validate data type
            QVariant convertedValue = convertToSqlType(value, field.type, field.name);
            if (!convertedValue.isValid() && !value.isNull()) {
                setError(QString("Failed to convert field '%1' to required type at batch index %2")
                        .arg(field.name).arg(i));
                return false;
            }
            
            validatedData.append(convertedValue);
        }
        
        validatedBatchData.append(validatedData);
    }
    
    QList<DatabaseFieldInfo> dbFields = convertToDbFieldInfo(schema);
    
    // Prepare column names for bulk insert
    QStringList columns;
    columns.append("original_line_number");
    for (const DatabaseFieldInfo& field : dbFields) {
        columns.append(field.name);
    }
    
    // Validate database before critical operation
    if (!validateDatabaseOperation()) {
        if (!handleDatabaseCorruption("bulk insert preparation")) {
            setError("Database validation failed before bulk insert");
            return false;
        }
    }
    
    if (!m_dbManager->prepareBulkInsert(tableName, columns)) {
        // Check if this might be due to corruption
        QString error = m_dbManager->lastError();
        if (error.contains("database disk image is malformed") || 
            error.contains("database corruption") ||
            error.contains("disk I/O error")) {
            handleDatabaseCorruption("bulk insert preparation");
        }
        setError(QString("Failed to prepare bulk insert: %1").arg(error));
        return false;
    }
    
    if (!m_dbManager->beginTransaction()) {
        setError("Failed to begin transaction");
        m_dbManager->finalizeBulkInsert();
        return false;
    }
    
    // Insert each validated record
    for (int i = 0; i < validatedBatchData.size(); ++i) {
        QVariantList params;
        params.append(lineNumbers[i]);
        params.append(validatedBatchData[i]);
        
        if (!m_dbManager->executeBulkInsert(params)) {
            setError(QString("Failed to execute bulk insert at batch index %1: %2")
                    .arg(i).arg(m_dbManager->lastError()));
            m_dbManager->rollbackTransaction();
            m_dbManager->finalizeBulkInsert();
            return false;
        }
    }
    
    if (!m_dbManager->commitTransaction()) {
        setError("Failed to commit transaction");
        m_dbManager->finalizeBulkInsert();
        return false;
    }
    
    m_dbManager->finalizeBulkInsert();
    
    // Reset error counter on successful batch operation
    m_consecutiveErrors = 0;
    return true;
}

bool PluginDatabaseManager::insertLargeBatchData(const QString& pluginName, const QList<QVariantList>& batchData, const QList<int>& lineNumbers, int chunkSize)
{
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    if (pluginName.isEmpty()) {
        setError("Plugin name cannot be empty");
        return false;
    }
    
    if (batchData.isEmpty()) {
        return true; // Empty batch is valid
    }
    
    if (batchData.size() != lineNumbers.size()) {
        setError(QString("Batch data size (%1) does not match line numbers size (%2)")
                .arg(batchData.size()).arg(lineNumbers.size()));
        return false;
    }
    
    if (chunkSize <= 0) {
        chunkSize = 1000; // Default chunk size
    }
    
    // Process data in chunks to optimize memory usage and transaction size
    int totalRecords = batchData.size();
    int processedRecords = 0;
    
    while (processedRecords < totalRecords) {
        int chunkEnd = qMin(processedRecords + chunkSize, totalRecords);
        int currentChunkSize = chunkEnd - processedRecords;
        
        // Extract chunk
        QList<QVariantList> chunkData;
        QList<int> chunkLineNumbers;
        
        for (int i = processedRecords; i < chunkEnd; ++i) {
            chunkData.append(batchData[i]);
            chunkLineNumbers.append(lineNumbers[i]);
        }
        
        // Insert chunk using regular batch insert
        if (!insertBatchData(pluginName, chunkData, chunkLineNumbers)) {
            setError(QString("Failed to insert chunk starting at record %1: %2")
                    .arg(processedRecords).arg(m_lastError));
            return false;
        }
        
        processedRecords += currentChunkSize;
        
        // Emit progress signal if needed (could be added later)
        // emit insertProgress(processedRecords, totalRecords);
    }
    
    return true;
}

QList<QVariantList> PluginDatabaseManager::queryData(const QString& pluginName, const FilterOptions& filter)
{
    QMutexLocker locker(&m_mutex);
    
    QList<QVariantList> results;
    
    // If in fallback mode, return empty results to trigger plugin fallback to in-memory storage
    if (m_fallbackMode) {
        return results;
    }
    
    if (!isReady()) {
        setError("Database not initialized");
        return results;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return results;
    }
    
    // Get plugin schema for query building
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return results;
    }
    
    // Build query using PluginQueryBuilder
    PluginQueryBuilder builder = PluginQueryBuilder::fromFilterOptions(filter, schema);
    QString sql = builder.buildQuery(tableName);
    QVariantList params = builder.getParameters();
    
    auto query = m_dbManager->executeQuery(sql, params);
    if (query.lastError().isValid()) {
        setError(QString("Query failed: %1").arg(query.lastError().text()));
        return results;
    }
    
    while (query.next()) {
        QVariantList row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.append(query.value(i));
        }
        results.append(row);
    }
    
    return results;
}

QSet<int> PluginDatabaseManager::getFilteredLineNumbers(const QString& pluginName, const FilterOptions& filter)
{
    QMutexLocker locker(&m_mutex);
    
    QSet<int> lineNumbers;
    
    if (!isReady()) {
        setError("Database not initialized");
        return lineNumbers;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return lineNumbers;
    }
    
    // Get plugin schema for query building
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return lineNumbers;
    }
    
    // Build query using PluginQueryBuilder for line numbers only
    PluginQueryBuilder builder = PluginQueryBuilder::fromFilterOptions(filter, schema);
    QStringList selectFields = QStringList() << "DISTINCT original_line_number";
    QString sql = builder.buildQuery(tableName, selectFields);
    QVariantList params = builder.getParameters();
    
    auto query = m_dbManager->executeQuery(sql, params);
    if (query.lastError().isValid()) {
        setError(QString("Query failed: %1").arg(query.lastError().text()));
        return lineNumbers;
    }
    
    while (query.next()) {
        lineNumbers.insert(query.value(0).toInt());
    }
    
    return lineNumbers;
}

QList<QVariantList> PluginDatabaseManager::queryDataWithPagination(const QString& pluginName, const FilterOptions& filter, int limit, int offset)
{
    QMutexLocker locker(&m_mutex);
    
    QList<QVariantList> results;
    
    if (!isReady()) {
        setError("Database not initialized");
        return results;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return results;
    }
    
    // Get plugin schema for query building
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return results;
    }
    
    // Build query using PluginQueryBuilder with pagination
    PluginQueryBuilder builder = PluginQueryBuilder::fromFilterOptions(filter, schema);
    if (limit > 0) {
        builder.limit(limit);
    }
    if (offset > 0) {
        builder.offset(offset);
    }
    
    QString sql = builder.buildQuery(tableName);
    QVariantList params = builder.getParameters();
    
    auto query = m_dbManager->executeQuery(sql, params);
    if (query.lastError().isValid()) {
        setError(QString("Query failed: %1").arg(query.lastError().text()));
        return results;
    }
    
    while (query.next()) {
        QVariantList row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.append(query.value(i));
        }
        results.append(row);
    }
    
    return results;
}

int PluginDatabaseManager::getFilteredRowCount(const QString& pluginName, const FilterOptions& filter)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isReady()) {
        setError("Database not initialized");
        return 0;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return 0;
    }
    
    // Get plugin schema for query building
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return 0;
    }
    
    // Build count query using PluginQueryBuilder
    PluginQueryBuilder builder = PluginQueryBuilder::fromFilterOptions(filter, schema);
    QString sql = builder.buildCountQuery(tableName);
    QVariantList params = builder.getParameters();
    
    auto query = m_dbManager->executeQuery(sql, params);
    if (query.lastError().isValid()) {
        setError(QString("Count query failed: %1").arg(query.lastError().text()));
        return 0;
    }
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

PluginDatabaseManager::QueryResult PluginDatabaseManager::queryDataWithMetadata(const QString& pluginName, const FilterOptions& filter, int limit, int offset)
{
    QMutexLocker locker(&m_mutex);
    
    QueryResult result;
    result.totalCount = 0;
    result.hasMore = false;
    
    if (!isReady()) {
        setError("Database not initialized");
        return result;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return result;
    }
    
    // Get plugin schema for query building
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return result;
    }
    
    // Get total count first
    result.totalCount = getFilteredRowCount(pluginName, filter);
    
    // Build main query with pagination
    PluginQueryBuilder builder = PluginQueryBuilder::fromFilterOptions(filter, schema);
    if (limit > 0) {
        builder.limit(limit);
    }
    if (offset > 0) {
        builder.offset(offset);
    }
    
    QString sql = builder.buildQuery(tableName);
    QVariantList params = builder.getParameters();
    
    auto query = m_dbManager->executeQuery(sql, params);
    if (query.lastError().isValid()) {
        setError(QString("Query failed: %1").arg(query.lastError().text()));
        return result;
    }
    
    while (query.next()) {
        QVariantList row;
        int lineNumber = 0;
        
        for (int i = 0; i < query.record().count(); ++i) {
            QVariant value = query.value(i);
            row.append(value);
            
            // Extract line number for mapping (assuming it's in column named 'original_line_number')
            if (query.record().fieldName(i) == "original_line_number") {
                lineNumber = value.toInt();
            }
        }
        
        result.data.append(row);
        if (lineNumber > 0) {
            result.lineNumbers.insert(lineNumber);
        }
    }
    
    // Check if there are more results
    if (limit > 0 && result.data.size() == limit) {
        result.hasMore = (offset + limit) < result.totalCount;
    }
    
    return result;
}

bool PluginDatabaseManager::updatePluginSchema(const QString& pluginName, const QList<FieldInfo>& newSchema)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    // Get current schema version
    int currentVersion = getPluginSchemaVersion(pluginName);
    int newVersion = currentVersion + 1;
    
    // Attempt sophisticated migration first
    if (currentVersion > 0) {
        if (migratePluginSchema(pluginName, currentVersion, newVersion, newSchema)) {
            return true;
        }
        
        // If migration fails, fall back to recreate
        qDebug() << "Schema migration failed for plugin" << pluginName << ", falling back to table recreation";
    }
    
    // Fallback: recreate table (data loss)
    QString tableName = getPluginTableName(pluginName);
    
    // Backup existing data if possible
    performSchemaBackup(pluginName);
    
    // Drop existing table
    if (!m_dbManager->executeNonQuery(QString("DROP TABLE IF EXISTS %1").arg(tableName))) {
        setError(QString("Failed to drop existing table: %1").arg(m_dbManager->lastError()));
        return false;
    }
    
    // Create new table with updated schema
    if (!createPluginTable(pluginName, newSchema)) {
        // Attempt to restore backup
        restoreSchemaBackup(pluginName);
        return false;
    }
    
    return true;
}

QList<FieldInfo> PluginDatabaseManager::getPluginSchema(const QString& pluginName)
{
    // Check cache first
    if (m_schemaCache.contains(pluginName)) {
        return m_schemaCache[pluginName];
    }
    
    // Load from database
    QString sql = "SELECT field_definitions FROM plugin_metadata WHERE plugin_name = ?";
    QVariant result = m_dbManager->executeScalar(sql, {pluginName});
    
    if (result.isNull()) {
        return QList<FieldInfo>();
    }
    
    // Parse JSON field definitions
    QJsonDocument doc = QJsonDocument::fromJson(result.toString().toUtf8());
    QJsonArray array = doc.array();
    
    QList<FieldInfo> schema;
    for (const QJsonValue& value : array) {
        QJsonObject obj = value.toObject();
        FieldInfo field;
        field.name = obj["name"].toString();
        field.type = static_cast<DataType>(obj["type"].toInt());
        schema.append(field);
    }
    
    // Cache the result
    m_schemaCache[pluginName] = schema;
    return schema;
}

bool PluginDatabaseManager::pluginTableExists(const QString& pluginName)
{
    QString tableName = getPluginTableName(pluginName);
    return m_dbManager && m_dbManager->tableExists(tableName);
}

QStringList PluginDatabaseManager::getPluginTables() const
{
    if (!m_dbManager) {
        return QStringList();
    }
    
    QString sql = "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'plugin_%_data'";
    QVariantList variants = m_dbManager->executeScalarList(sql);
    QStringList result;
    for (const QVariant& variant : variants) {
        result.append(variant.toString());
    }
    return result;
}

bool PluginDatabaseManager::createIndexForField(const QString& pluginName, const QString& fieldName)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return false;
    }
    
    QString indexName = QString("idx_%1_%2").arg(pluginName, fieldName);
    QString sql = QString("CREATE INDEX IF NOT EXISTS %1 ON %2(%3)")
                  .arg(indexName, tableName, fieldName);
    
    auto query = m_dbManager->executeQuery(sql);
    if (query.lastError().isValid()) {
        setError(QString("Failed to create index: %1").arg(query.lastError().text()));
        return false;
    }
    
    return true;
}

bool PluginDatabaseManager::dropIndexForField(const QString& pluginName, const QString& fieldName)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    QString indexName = QString("idx_%1_%2").arg(pluginName, fieldName);
    QString sql = QString("DROP INDEX IF EXISTS %1").arg(indexName);
    
    auto query = m_dbManager->executeQuery(sql);
    if (query.lastError().isValid()) {
        setError(QString("Failed to drop index: %1").arg(query.lastError().text()));
        return false;
    }
    
    return true;
}

QStringList PluginDatabaseManager::getIndexesForPlugin(const QString& pluginName)
{
    QMutexLocker locker(&m_mutex);
    
    QStringList indexes;
    
    if (!isReady()) {
        setError("Database not initialized");
        return indexes;
    }
    
    QString tableName = getPluginTableName(pluginName);
    QString sql = QString("SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='%1'")
                  .arg(tableName);
    
    auto query = m_dbManager->executeQuery(sql);
    if (query.lastError().isValid()) {
        setError(QString("Failed to get indexes: %1").arg(query.lastError().text()));
        return indexes;
    }
    
    while (query.next()) {
        indexes.append(query.value(0).toString());
    }
    
    return indexes;
}

void PluginDatabaseManager::handleDatabaseError(const QString& error)
{
    setError(QString("Database error: %1").arg(error));
}

void PluginDatabaseManager::handleDatabaseStatusChanged(DatabaseStatus status)
{
    switch (status) {
        case DatabaseStatus::Ready:
            if (m_status == PluginDatabaseStatus::Initializing) {
                setStatus(PluginDatabaseStatus::Ready);
            }
            break;
        case DatabaseStatus::Error:
            setStatus(PluginDatabaseStatus::Error);
            break;
        default:
            break;
    }
}

void PluginDatabaseManager::setStatus(PluginDatabaseStatus status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged(status);
    }
}

void PluginDatabaseManager::setError(const QString& error)
{
    m_lastError = error;
    m_consecutiveErrors++;
    
    // Enable fallback mode if we have too many consecutive errors
    if (m_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS && !m_fallbackMode) {
        enableFallbackMode(QString("Too many consecutive database errors (%1)").arg(m_consecutiveErrors));
    }
    
    setStatus(PluginDatabaseStatus::Error);
    emit errorOccurred(error);
    qDebug() << "PluginDatabaseManager error:" << error;
}

QString PluginDatabaseManager::generateDatabasePath(const QString& filePath)
{
    // Generate unique database path based on file path hash
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(filePath.toUtf8());
    QString hashString = hash.result().toHex();
    
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir + "/plugin_databases");
    
    return QString("%1/plugin_databases/logdor_plugins_%2.db")
           .arg(dataDir, hashString);
}

QString PluginDatabaseManager::getPluginTableName(const QString& pluginName)
{
    // Sanitize plugin name for use as table name
    QString sanitized = pluginName.toLower();
    sanitized.replace(QRegularExpression("[^a-z0-9_]"), "_");
    return QString("plugin_%1_data").arg(sanitized);
}

bool PluginDatabaseManager::createMetadataTables()
{
    // Metadata tables are created in the base schema during initialization
    return true;
}

bool PluginDatabaseManager::savePluginSchema(const QString& pluginName, const QList<FieldInfo>& schema)
{
    // Convert schema to JSON
    QJsonArray array;
    for (const FieldInfo& field : schema) {
        QJsonObject obj;
        obj["name"] = field.name;
        obj["type"] = static_cast<int>(field.type);
        array.append(obj);
    }
    
    QJsonDocument doc(array);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    
    // Insert or update schema metadata
    QString sql = R"(
        INSERT OR REPLACE INTO plugin_metadata 
        (plugin_name, schema_version, field_definitions, last_updated) 
        VALUES (?, 1, ?, CURRENT_TIMESTAMP)
    )";
    
    return m_dbManager->executeNonQuery(sql, {pluginName, jsonString});
}

QList<DatabaseFieldInfo> PluginDatabaseManager::convertToDbFieldInfo(const QList<FieldInfo>& fieldInfo)
{
    QList<DatabaseFieldInfo> dbFields;
    
    for (const FieldInfo& field : fieldInfo) {
        DatabaseFieldInfo dbField;
        dbField.name = field.name;
        dbField.type = field.type;
        dbField.indexed = (field.type == DataType::String); // Index string fields by default
        dbField.nullable = true;
        dbField.sqlType = dbField.generateSqlType();
        dbFields.append(dbField);
    }
    
    return dbFields;
}

QString PluginDatabaseManager::generateCreateTableSql(const QString& tableName, const QList<DatabaseFieldInfo>& fields)
{
    QStringList columns;
    
    // Add standard columns
    columns.append("id INTEGER PRIMARY KEY AUTOINCREMENT");
    columns.append("original_line_number INTEGER NOT NULL");
    columns.append("created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP");
    
    // Add plugin-specific columns
    for (const DatabaseFieldInfo& field : fields) {
        QString column = QString("%1 %2").arg(field.name, field.sqlType);
        if (!field.nullable) {
            column += " NOT NULL";
        }
        if (!field.defaultValue.isNull()) {
            column += QString(" DEFAULT %1").arg(field.defaultValue.toString());
        }
        columns.append(column);
    }
    
    return QString("CREATE TABLE %1 (%2)").arg(tableName, columns.join(", "));
}

QString PluginDatabaseManager::generateInsertSql(const QString& tableName, const QList<DatabaseFieldInfo>& fields)
{
    QStringList columns;
    QStringList placeholders;
    
    columns.append("original_line_number");
    placeholders.append("?");
    
    for (const DatabaseFieldInfo& field : fields) {
        columns.append(field.name);
        placeholders.append("?");
    }
    
    return QString("INSERT INTO %1 (%2) VALUES (%3)")
           .arg(tableName, columns.join(", "), placeholders.join(", "));
}

bool PluginDatabaseManager::createIndexes(const QString& tableName, const QList<DatabaseFieldInfo>& fields)
{
    // Create index on original_line_number (always needed)
    QString lineIndexSql = QString("CREATE INDEX IF NOT EXISTS idx_%1_line_number ON %1(original_line_number)")
                          .arg(tableName);
    
    if (!m_dbManager->executeNonQuery(lineIndexSql)) {
        return false;
    }
    
    // Create indexes on indexed fields
    for (const DatabaseFieldInfo& field : fields) {
        if (field.indexed) {
            QString indexSql = QString("CREATE INDEX IF NOT EXISTS idx_%1_%2 ON %1(%2)")
                              .arg(tableName, field.name);
            
            if (!m_dbManager->executeNonQuery(indexSql)) {
                return false;
            }
        }
    }
    
    return true;
}

int PluginDatabaseManager::getPluginSchemaVersion(const QString& pluginName)
{
    QString sql = "SELECT schema_version FROM plugin_metadata WHERE plugin_name = ?";
    QVariant result = m_dbManager->executeScalar(sql, {pluginName});
    return result.isNull() ? 0 : result.toInt();
}

bool PluginDatabaseManager::migratePluginSchema(const QString& pluginName, int fromVersion, int toVersion, const QList<FieldInfo>& newSchema)
{
    QString tableName = getPluginTableName(pluginName);
    
    // Get current schema
    QList<FieldInfo> currentSchema = getPluginSchema(pluginName);
    if (currentSchema.isEmpty()) {
        setError("Cannot migrate: current schema not found");
        return false;
    }
    
    // Convert schemas to database field info
    QList<DatabaseFieldInfo> oldFields = convertToDbFieldInfo(currentSchema);
    QList<DatabaseFieldInfo> newFields = convertToDbFieldInfo(newSchema);
    
    // Generate migration commands
    QStringList migrationCommands = generateMigrationCommands(tableName, oldFields, newFields);
    
    if (migrationCommands.isEmpty()) {
        // No changes needed
        return savePluginSchema(pluginName, newSchema);
    }
    
    // Execute migration in transaction
    if (!m_dbManager->beginTransaction()) {
        setError("Failed to begin migration transaction");
        return false;
    }
    
    // Execute each migration command
    for (const QString& command : migrationCommands) {
        if (!m_dbManager->executeNonQuery(command)) {
            setError(QString("Migration command failed: %1 - %2").arg(command, m_dbManager->lastError()));
            m_dbManager->rollbackTransaction();
            return false;
        }
    }
    
    // Update schema metadata
    if (!savePluginSchema(pluginName, newSchema)) {
        setError("Failed to save updated schema metadata");
        m_dbManager->rollbackTransaction();
        return false;
    }
    
    // Update schema version
    QString updateVersionSql = "UPDATE plugin_metadata SET schema_version = ? WHERE plugin_name = ?";
    if (!m_dbManager->executeNonQuery(updateVersionSql, {toVersion, pluginName})) {
        setError("Failed to update schema version");
        m_dbManager->rollbackTransaction();
        return false;
    }
    
    if (!m_dbManager->commitTransaction()) {
        setError("Failed to commit migration transaction");
        return false;
    }
    
    // Update cache
    m_schemaCache[pluginName] = newSchema;
    
    return true;
}

bool PluginDatabaseManager::performSchemaBackup(const QString& pluginName)
{
    QString tableName = getPluginTableName(pluginName);
    QString backupTableName = tableName + "_backup";
    
    // Create backup table
    QString backupSql = QString("CREATE TABLE IF NOT EXISTS %1 AS SELECT * FROM %2")
                       .arg(backupTableName, tableName);
    
    return m_dbManager->executeNonQuery(backupSql);
}

bool PluginDatabaseManager::restoreSchemaBackup(const QString& pluginName)
{
    QString tableName = getPluginTableName(pluginName);
    QString backupTableName = tableName + "_backup";
    
    if (!m_dbManager->tableExists(backupTableName)) {
        return false; // No backup available
    }
    
    // Drop current table and restore from backup
    if (!m_dbManager->executeNonQuery(QString("DROP TABLE IF EXISTS %1").arg(tableName))) {
        return false;
    }
    
    QString restoreSql = QString("ALTER TABLE %1 RENAME TO %2")
                        .arg(backupTableName, tableName);
    
    return m_dbManager->executeNonQuery(restoreSql);
}

QStringList PluginDatabaseManager::generateMigrationCommands(const QString& tableName, 
                                                           const QList<DatabaseFieldInfo>& oldFields, 
                                                           const QList<DatabaseFieldInfo>& newFields)
{
    QStringList commands;
    
    // Create maps for easier lookup
    QMap<QString, DatabaseFieldInfo> oldFieldMap;
    QMap<QString, DatabaseFieldInfo> newFieldMap;
    
    for (const DatabaseFieldInfo& field : oldFields) {
        oldFieldMap[field.name] = field;
    }
    
    for (const DatabaseFieldInfo& field : newFields) {
        newFieldMap[field.name] = field;
    }
    
    // Find new columns to add
    for (const DatabaseFieldInfo& newField : newFields) {
        if (!oldFieldMap.contains(newField.name)) {
            QString addColumnSql = QString("ALTER TABLE %1 ADD COLUMN %2 %3")
                                  .arg(tableName, newField.name, newField.sqlType);
            
            if (!newField.nullable) {
                addColumnSql += " NOT NULL";
            }
            
            if (!newField.defaultValue.isNull()) {
                addColumnSql += QString(" DEFAULT %1").arg(newField.defaultValue.toString());
            }
            
            commands.append(addColumnSql);
            
            // Create index if needed
            if (newField.indexed) {
                QString indexSql = QString("CREATE INDEX IF NOT EXISTS idx_%1_%2 ON %1(%2)")
                                  .arg(tableName, newField.name);
                commands.append(indexSql);
            }
        }
    }
    
    // Note: SQLite doesn't support dropping columns easily, so we don't handle removed columns
    // In a production system, this would require creating a new table and copying data
    
    // Find modified columns (type changes, etc.)
    for (const DatabaseFieldInfo& newField : newFields) {
        if (oldFieldMap.contains(newField.name)) {
            const DatabaseFieldInfo& oldField = oldFieldMap[newField.name];
            
            // Check if index status changed
            if (newField.indexed && !oldField.indexed) {
                QString indexSql = QString("CREATE INDEX IF NOT EXISTS idx_%1_%2 ON %1(%2)")
                                  .arg(tableName, newField.name);
                commands.append(indexSql);
            } else if (!newField.indexed && oldField.indexed) {
                QString dropIndexSql = QString("DROP INDEX IF EXISTS idx_%1_%2")
                                      .arg(tableName, newField.name);
                commands.append(dropIndexSql);
            }
        }
    }
    
    return commands;
}

QVariant PluginDatabaseManager::convertToSqlType(const QVariant& value, DataType targetType, const QString& fieldName)
{
    // Handle null values
    if (value.isNull()) {
        return QVariant();
    }
    
    switch (targetType) {
        case DataType::String: {
            // Convert to string
            QString stringValue = value.toString();
            return stringValue;
        }
        
        case DataType::Integer: {
            // Convert to integer
            bool ok = false;
            int intValue = value.toInt(&ok);
            if (!ok) {
                qDebug() << "Warning: Failed to convert field" << fieldName 
                         << "value" << value << "to integer, using 0";
                return 0;
            }
            return intValue;
        }
        
        case DataType::DateTime: {
            // Convert to datetime
            if (value.metaType() == QMetaType::fromType<QDateTime>()) {
                return value;
            } else if (value.metaType() == QMetaType::fromType<QString>()) {
                QDateTime dateTime = QDateTime::fromString(value.toString(), Qt::ISODate);
                if (!dateTime.isValid()) {
                    // Try other common formats
                    dateTime = QDateTime::fromString(value.toString(), "yyyy-MM-dd hh:mm:ss");
                    if (!dateTime.isValid()) {
                        dateTime = QDateTime::fromString(value.toString(), "yyyy-MM-dd");
                        if (!dateTime.isValid()) {
                            qDebug() << "Warning: Failed to convert field" << fieldName 
                                     << "value" << value << "to datetime, using current time";
                            return QDateTime::currentDateTime();
                        }
                    }
                }
                return dateTime;
            } else {
                qDebug() << "Warning: Unsupported type for datetime conversion in field" << fieldName
                         << ", using current time";
                return QDateTime::currentDateTime();
            }
        }
        
        default:
            // Unknown type, return as string
            qDebug() << "Warning: Unknown data type for field" << fieldName << ", treating as string";
            return value.toString();
    }
}

// Error handling and fallback methods
void PluginDatabaseManager::enableFallbackMode(const QString& reason)
{
    if (!m_fallbackMode) {
        m_fallbackMode = true;
        m_fallbackReason = reason;
        qWarning() << "PluginDatabaseManager: Enabling fallback mode -" << reason;
        emit fallbackModeEnabled(reason);
    }
}

void PluginDatabaseManager::disableFallbackMode()
{
    if (m_fallbackMode) {
        m_fallbackMode = false;
        m_fallbackReason.clear();
        m_consecutiveErrors = 0;
        qDebug() << "PluginDatabaseManager: Fallback mode disabled";
        emit fallbackModeDisabled();
    }
}

bool PluginDatabaseManager::checkDatabaseIntegrity()
{
    if (!m_dbManager || !isReady()) {
        setError("Database not ready for integrity check");
        return false;
    }
    
    // Use SQLite PRAGMA integrity_check
    auto query = m_dbManager->executeQuery("PRAGMA integrity_check");
    if (query.lastError().isValid()) {
        setError(QString("Failed to run integrity check: %1").arg(query.lastError().text()));
        return false;
    }
    
    bool isIntact = true;
    while (query.next()) {
        QString result = query.value(0).toString();
        if (result != "ok") {
            qWarning() << "Database integrity issue:" << result;
            isIntact = false;
        }
    }
    
    if (!isIntact) {
        setError("Database integrity check failed - corruption detected");
        return false;
    }
    
    qDebug() << "Database integrity check passed";
    return true;
}

bool PluginDatabaseManager::repairDatabase()
{
    if (!m_dbManager || !isReady()) {
        setError("Database not ready for repair");
        return false;
    }
    
    qDebug() << "Attempting database repair...";
    
    // Try to repair using SQLite REINDEX
    if (!m_dbManager->executeNonQuery("REINDEX")) {
        setError(QString("Failed to reindex database: %1").arg(m_dbManager->lastError()));
        return false;
    }
    
    // Run integrity check after repair
    if (!checkDatabaseIntegrity()) {
        setError("Database repair failed - integrity check still fails");
        return false;
    }
    
    // Reset error counter on successful repair
    m_consecutiveErrors = 0;
    qDebug() << "Database repair completed successfully";
    emit databaseRepaired();
    return true;
}

bool PluginDatabaseManager::rebuildDatabaseFromFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        setError("Cannot rebuild database - no source file specified");
        return false;
    }
    
    qDebug() << "Rebuilding database from file:" << filePath;
    
    // Close current database
    closeDatabase();
    
    // Remove corrupted database file
    QFile::remove(m_databasePath);
    
    // Reinitialize database
    if (!initializeForFile(filePath)) {
        setError("Failed to reinitialize database during rebuild");
        return false;
    }
    
    // Reset error counter and disable fallback mode
    m_consecutiveErrors = 0;
    disableFallbackMode();
    
    qDebug() << "Database rebuild completed successfully";
    emit databaseRebuilt();
    return true;
}

bool PluginDatabaseManager::optimizeDatabase()
{
    if (!m_dbManager || !isReady()) {
        setError("Database not ready for optimization");
        return false;
    }
    
    qDebug() << "Optimizing database...";
    
    // Run ANALYZE to update query planner statistics
    if (!m_dbManager->executeNonQuery("ANALYZE")) {
        qWarning() << "Failed to analyze database:" << m_dbManager->lastError();
        // Don't fail completely, continue with other optimizations
    }
    
    // Run VACUUM to reclaim space and defragment
    if (!m_dbManager->executeNonQuery("VACUUM")) {
        qWarning() << "Failed to vacuum database:" << m_dbManager->lastError();
        return false;
    }
    
    qDebug() << "Database optimization completed";
    return true;
}

bool PluginDatabaseManager::vacuumDatabase()
{
    if (!m_dbManager || !isReady()) {
        setError("Database not ready for vacuum");
        return false;
    }
    
    qDebug() << "Vacuuming database...";
    
    if (!m_dbManager->executeNonQuery("VACUUM")) {
        setError(QString("Failed to vacuum database: %1").arg(m_dbManager->lastError()));
        return false;
    }
    
    qDebug() << "Database vacuum completed";
    return true;
}

qint64 PluginDatabaseManager::getDatabaseSize() const
{
    if (m_databasePath.isEmpty()) {
        return 0;
    }
    
    QFileInfo fileInfo(m_databasePath);
    return fileInfo.exists() ? fileInfo.size() : 0;
}

QString PluginDatabaseManager::getDatabaseVersion() const
{
    if (!m_dbManager || !isReady()) {
        return QString();
    }
    
    auto query = m_dbManager->executeQuery("PRAGMA user_version");
    if (query.lastError().isValid() || !query.next()) {
        return QString();
    }
    
    return query.value(0).toString();
}

// Error handling and recovery helper methods
bool PluginDatabaseManager::validateDatabaseOperation()
{
    if (!m_dbManager || !isReady()) {
        return false;
    }
    
    // Quick integrity check using a simple query
    auto query = m_dbManager->executeQuery("SELECT COUNT(*) FROM sqlite_master");
    if (query.lastError().isValid()) {
        qWarning() << "Database validation failed:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool PluginDatabaseManager::handleDatabaseCorruption(const QString& operation)
{
    qWarning() << "Database corruption detected during operation:" << operation;
    
    // Enable fallback mode immediately
    enableFallbackMode(QString("Database corruption detected during %1").arg(operation));
    
    // Attempt repair in background
    if (repairDatabase()) {
        qDebug() << "Database repair successful after corruption";
        return true;
    }
    
    // If repair fails, suggest rebuild
    qWarning() << "Database repair failed, rebuild required";
    emit errorOccurred(QString("Database corruption detected during %1. Rebuild required.").arg(operation));
    
    return false;
}

void PluginDatabaseManager::recordFileMetadata(const QString& filePath)
{
    if (!m_dbManager || !isReady()) {
        return;
    }
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return;
    }
    
    QString sql = R"(
        INSERT OR REPLACE INTO file_metadata 
        (file_path, file_size, file_modified, database_version, created_at) 
        VALUES (?, ?, ?, ?, ?)
    )";
    
    QVariantList params;
    params << filePath 
           << fileInfo.size() 
           << fileInfo.lastModified() 
           << 1 // database version
           << QDateTime::currentDateTime();
    
    if (!m_dbManager->executeNonQuery(sql, params)) {
        qWarning() << "Failed to record file metadata:" << m_dbManager->lastError();
    }
}bool 
PluginDatabaseManager::performMaintenanceCheck()
{
    if (!m_dbManager || !isReady()) {
        return false;
    }
    
    qDebug() << "Performing database maintenance check...";
    
    // Check integrity
    if (!checkDatabaseIntegrity()) {
        qWarning() << "Maintenance check: integrity check failed";
        return false;
    }
    
    // Check database size and suggest optimization if needed
    qint64 size = getDatabaseSize();
    if (size > 100 * 1024 * 1024) { // 100MB threshold
        qDebug() << "Database size is" << (size / 1024 / 1024) << "MB, consider optimization";
        
        // Auto-optimize if database is very large
        if (size > 500 * 1024 * 1024) { // 500MB threshold
            qDebug() << "Auto-optimizing large database...";
            optimizeDatabase();
        }
    }
    
    // Check for unused space and vacuum if needed
    auto query = m_dbManager->executeQuery("PRAGMA freelist_count");
    if (!query.lastError().isValid() && query.next()) {
        int freePages = query.value(0).toInt();
        if (freePages > 1000) { // Significant unused space
            qDebug() << "Database has" << freePages << "free pages, running vacuum...";
            vacuumDatabase();
        }
    }
    
    qDebug() << "Database maintenance check completed";
    return true;
}

void PluginDatabaseManager::schedulePeriodicMaintenance()
{
    // This could be enhanced with a QTimer for periodic checks
    // For now, we'll rely on manual calls during operations
    qDebug() << "Periodic maintenance scheduling not yet implemented";
}