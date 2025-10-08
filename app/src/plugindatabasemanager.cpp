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

PluginDatabaseManager::PluginDatabaseManager(QObject* parent)
    : QObject(parent)
    , m_status(PluginDatabaseStatus::NotInitialized)
    , m_dbManager(nullptr)
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
    
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return false;
    }
    
    // Get schema to build proper insert
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return false;
    }
    
    QList<DatabaseFieldInfo> dbFields = convertToDbFieldInfo(schema);
    QString insertSql = generateInsertSql(tableName, dbFields);
    
    // Prepare parameters: original_line_number + data
    QVariantList params;
    params.append(originalLineNumber);
    params.append(data);
    
    if (!m_dbManager->executeNonQuery(insertSql, params)) {
        setError(QString("Failed to insert data: %1").arg(m_dbManager->lastError()));
        return false;
    }
    
    return true;
}

bool PluginDatabaseManager::insertBatchData(const QString& pluginName, const QList<QVariantList>& batchData, const QList<int>& lineNumbers)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isReady()) {
        setError("Database not initialized");
        return false;
    }
    
    if (batchData.size() != lineNumbers.size()) {
        setError("Batch data and line numbers size mismatch");
        return false;
    }
    
    QString tableName = getPluginTableName(pluginName);
    
    if (!m_dbManager->tableExists(tableName)) {
        setError(QString("Plugin table does not exist: %1").arg(tableName));
        return false;
    }
    
    // Get schema
    QList<FieldInfo> schema = getPluginSchema(pluginName);
    if (schema.isEmpty()) {
        setError(QString("No schema found for plugin: %1").arg(pluginName));
        return false;
    }
    
    QList<DatabaseFieldInfo> dbFields = convertToDbFieldInfo(schema);
    
    // Prepare column names for bulk insert
    QStringList columns;
    columns.append("original_line_number");
    for (const DatabaseFieldInfo& field : dbFields) {
        columns.append(field.name);
    }
    
    if (!m_dbManager->prepareBulkInsert(tableName, columns)) {
        setError(QString("Failed to prepare bulk insert: %1").arg(m_dbManager->lastError()));
        return false;
    }
    
    if (!m_dbManager->beginTransaction()) {
        setError("Failed to begin transaction");
        m_dbManager->finalizeBulkInsert();
        return false;
    }
    
    // Insert each record
    for (int i = 0; i < batchData.size(); ++i) {
        QVariantList params;
        params.append(lineNumbers[i]);
        params.append(batchData[i]);
        
        if (!m_dbManager->executeBulkInsert(params)) {
            setError(QString("Failed to execute bulk insert: %1").arg(m_dbManager->lastError()));
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
    return true;
}

QList<QVariantList> PluginDatabaseManager::queryData(const QString& pluginName, const FilterOptions& filter)
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
    
    // Build query based on filter options
    QString sql = QString("SELECT * FROM %1").arg(tableName);
    QVariantList params;
    
    if (!filter.query.isEmpty()) {
        // For now, implement basic text search across all text fields
        // This is a simplified implementation - more sophisticated filtering
        // would be implemented in task 3
        sql += " WHERE 1=1"; // Placeholder for more complex filtering
        
        // Add basic text search if query is provided
        QList<FieldInfo> schema = getPluginSchema(pluginName);
        QStringList textConditions;
        
        for (const FieldInfo& field : schema) {
            if (field.type == DataType::String) {
                if (filter.caseSensitivity == Qt::CaseInsensitive) {
                    textConditions.append(QString("LOWER(%1) LIKE LOWER(?)").arg(field.name));
                } else {
                    textConditions.append(QString("%1 LIKE ?").arg(field.name));
                }
                params.append(QString("%%1%").arg(filter.query));
            }
        }
        
        if (!textConditions.isEmpty()) {
            sql += " AND (" + textConditions.join(" OR ") + ")";
        }
    }
    
    sql += " ORDER BY original_line_number";
    
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
    
    // Build query to get only line numbers
    QString sql = QString("SELECT DISTINCT original_line_number FROM %1").arg(tableName);
    QVariantList params;
    
    if (!filter.query.isEmpty()) {
        sql += " WHERE 1=1";
        
        QList<FieldInfo> schema = getPluginSchema(pluginName);
        QStringList textConditions;
        
        for (const FieldInfo& field : schema) {
            if (field.type == DataType::String) {
                if (filter.caseSensitivity == Qt::CaseInsensitive) {
                    textConditions.append(QString("LOWER(%1) LIKE LOWER(?)").arg(field.name));
                } else {
                    textConditions.append(QString("%1 LIKE ?").arg(field.name));
                }
                params.append(QString("%%1%").arg(filter.query));
            }
        }
        
        if (!textConditions.isEmpty()) {
            sql += " AND (" + textConditions.join(" OR ") + ")";
        }
    }
    
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