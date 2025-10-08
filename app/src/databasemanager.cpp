#include "databasemanager.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSqlRecord>
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QMutexLocker>

int DatabaseManager::s_connectionCounter = 0;

DatabaseManager::DatabaseManager(const QString& databasePath, QObject* parent)
    : QObject(parent)
    , m_databasePath(databasePath)
    , m_status(DatabaseStatus::NotInitialized)
    , m_bulkQuery(nullptr)
{
    m_connectionName = generateConnectionName();
}

DatabaseManager::~DatabaseManager()
{
    closeDatabase();
}

bool DatabaseManager::createDatabase(const QString& schema)
{
    setStatus(DatabaseStatus::Initializing);
    
    // Ensure directory exists
    QFileInfo fileInfo(m_databasePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(dir.absolutePath())) {
            setError(QString("Failed to create directory: %1").arg(dir.absolutePath()));
            return false;
        }
    }

    if (!openDatabase()) {
        return false;
    }

    if (!createTables(schema)) {
        closeDatabase();
        return false;
    }

    if (!createSearchTables()) {
        closeDatabase();
        return false;
    }

    setStatus(DatabaseStatus::Ready);
    return true;
}

bool DatabaseManager::openDatabase()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_database.isOpen()) {
        return true;
    }

    m_database = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_database.setDatabaseName(m_databasePath);

    if (!m_database.open()) {
        setError(QString("Failed to open database: %1").arg(m_database.lastError().text()));
        return false;
    }

    // Enable foreign keys and set pragmas for performance
    executeNonQuery("PRAGMA foreign_keys = ON");
    executeNonQuery("PRAGMA synchronous = NORMAL"); 
    executeNonQuery("PRAGMA journal_mode = WAL");
    executeNonQuery("PRAGMA cache_size = 10000");

    return true;
}

void DatabaseManager::closeDatabase()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_bulkQuery) {
        delete m_bulkQuery;
        m_bulkQuery = nullptr;
    }

    if (m_database.isOpen()) {
        m_database.close();
    }
    
    QSqlDatabase::removeDatabase(m_connectionName);
    setStatus(DatabaseStatus::NotInitialized);
}

bool DatabaseManager::createTables(const QString& schema)
{
    if (schema.isEmpty()) {
        return true;
    }

    QStringList statements = schema.split(';', Qt::SkipEmptyParts);
    
    if (!beginTransaction()) {
        return false;
    }

    for (const QString& statement : statements) {
        QString trimmed = statement.trimmed();
        if (!trimmed.isEmpty()) {
            if (!executeNonQuery(trimmed)) {
                rollbackTransaction();
                return false;
            }
        }
    }

    return commitTransaction();
}

bool DatabaseManager::createSearchTables()
{
    QString searchSchema = R"(
        CREATE TABLE IF NOT EXISTS search_tokens (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            entry_id INTEGER NOT NULL,
            field_name TEXT NOT NULL,
            token TEXT NOT NULL,
            position INTEGER NOT NULL,
            FOREIGN KEY (entry_id) REFERENCES parsed_entries (id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_search_tokens_token ON search_tokens(token);
        CREATE INDEX IF NOT EXISTS idx_search_tokens_entry ON search_tokens(entry_id);
        CREATE INDEX IF NOT EXISTS idx_search_tokens_field ON search_tokens(field_name);

        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER NOT NULL
        );
    )";

    return createTables(searchSchema);
}

bool DatabaseManager::migrateSchema(int fromVersion, int toVersion, const QStringList& migrationCommands)
{
    int currentVersion = getSchemaVersion();
    if (currentVersion != fromVersion) {
        setError(QString("Schema version mismatch. Expected %1, got %2").arg(fromVersion).arg(currentVersion));
        return false;
    }

    if (!beginTransaction()) {
        return false;
    }

    for (const QString& command : migrationCommands) {
        if (!executeNonQuery(command.trimmed())) {
            rollbackTransaction();
            return false;
        }
    }

    if (!updateSchemaVersion(toVersion)) {
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
}

int DatabaseManager::getSchemaVersion()
{
    if (!tableExists("schema_version")) {
        return 0;
    }

    QVariant version = executeScalar("SELECT version FROM schema_version LIMIT 1");
    return version.toInt();
}

bool DatabaseManager::updateSchemaVersion(int version)
{
    if (!executeNonQuery("DELETE FROM schema_version")) {
        return false;
    }
    return executeNonQuery("INSERT INTO schema_version (version) VALUES (?)", {version});
}

QSqlQuery DatabaseManager::executeQuery(const QString& sql, const QVariantList& params)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_database.isOpen()) {
        QSqlQuery invalidQuery;
        return invalidQuery;
    }

    QSqlQuery query(m_database);
    query.prepare(sql);
    
    for (const QVariant& param : params) {
        query.addBindValue(param);
    }

    if (!query.exec()) {
        setError(QString("Query failed: %1. SQL: %2").arg(query.lastError().text(), sql));
        qDebug() << "Failed query:" << sql;
        qDebug() << "Parameters:" << params;
        qDebug() << "Error:" << query.lastError().text();
    }

    return query;
}

bool DatabaseManager::executeNonQuery(const QString& sql, const QVariantList& params)
{
    QSqlQuery query = executeQuery(sql, params);
    return !query.lastError().isValid();
}

QVariantList DatabaseManager::executeScalarList(const QString& sql, const QVariantList& params)
{
    QVariantList results;
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.lastError().isValid()) {
        return results;
    }

    while (query.next()) {
        results.append(query.value(0));
    }

    return results;
}

QVariant DatabaseManager::executeScalar(const QString& sql, const QVariantList& params)
{
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.lastError().isValid()) {
        return QVariant();
    }

    if (query.next()) {
        return query.value(0);
    }

    return QVariant();
}

bool DatabaseManager::beginTransaction()
{
    QMutexLocker locker(&m_mutex);
    return m_database.transaction();
}

bool DatabaseManager::commitTransaction()
{
    QMutexLocker locker(&m_mutex);
    return m_database.commit();
}

bool DatabaseManager::rollbackTransaction()
{
    QMutexLocker locker(&m_mutex);
    return m_database.rollback();
}

bool DatabaseManager::prepareBulkInsert(const QString& tableName, const QStringList& columns)
{
    if (m_bulkQuery) {
        delete m_bulkQuery;
    }

    m_bulkTableName = tableName;
    m_bulkColumns = columns;

    QString placeholders = generatePlaceholders(columns.size());
    QString sql = QString("INSERT INTO %1 (%2) VALUES (%3)")
                      .arg(tableName, columns.join(", "), placeholders);

    m_bulkQuery = new QSqlQuery(m_database);
    if (!m_bulkQuery->prepare(sql)) {
        setError(QString("Failed to prepare bulk insert: %1").arg(m_bulkQuery->lastError().text()));
        delete m_bulkQuery;
        m_bulkQuery = nullptr;
        return false;
    }

    return true;
}

bool DatabaseManager::executeBulkInsert(const QVariantList& values)
{
    if (!m_bulkQuery) {
        setError("No bulk insert prepared");
        return false;
    }

    if (values.size() != m_bulkColumns.size()) {
        setError(QString("Parameter count mismatch. Expected %1, got %2")
                     .arg(m_bulkColumns.size()).arg(values.size()));
        return false;
    }

    for (const QVariant& value : values) {
        m_bulkQuery->addBindValue(value);
    }

    if (!m_bulkQuery->exec()) {
        setError(QString("Bulk insert failed: %1").arg(m_bulkQuery->lastError().text()));
        return false;
    }

    return true;
}

bool DatabaseManager::finalizeBulkInsert()
{
    if (m_bulkQuery) {
        delete m_bulkQuery;
        m_bulkQuery = nullptr;
    }
    return true;
}

QList<SearchResult> DatabaseManager::performSearch(const SearchQuery& searchQuery, const QString& tableName)
{
    QList<SearchResult> results;
    
    if (searchQuery.query.isEmpty()) {
        return results;
    }

    QString sql;
    QVariantList params;

    if (searchQuery.fuzzyMatch) {
        // Use LIKE for fuzzy matching
        sql = R"(
            SELECT DISTINCT st.entry_id, pe.source_line_number, 
                   COUNT(*) as match_count,
                   GROUP_CONCAT(DISTINCT st.field_name) as matched_fields
            FROM search_tokens st
            JOIN parsed_entries pe ON st.entry_id = pe.id
            WHERE st.token LIKE ?
        )";
        
        if (!searchQuery.fields.isEmpty()) {
            sql += " AND st.field_name IN (" + generatePlaceholders(searchQuery.fields.size()) + ")";
            for (const QString& field : searchQuery.fields) {
                params.append(field);
            }
        }
        
        sql += R"(
            GROUP BY st.entry_id, pe.source_line_number
            ORDER BY match_count DESC
        )";
        
        if (searchQuery.maxResults > 0) {
            sql += QString(" LIMIT %1").arg(searchQuery.maxResults);
        }

        QString searchTerm = "%" + searchQuery.query + "%";
        params.prepend(searchTerm);
    } else {
        // Exact token matching
        sql = R"(
            SELECT DISTINCT st.entry_id, pe.source_line_number,
                   COUNT(*) as match_count,
                   GROUP_CONCAT(DISTINCT st.field_name) as matched_fields
            FROM search_tokens st
            JOIN parsed_entries pe ON st.entry_id = pe.id
            WHERE st.token = ?
        )";
        
        if (!searchQuery.fields.isEmpty()) {
            sql += " AND st.field_name IN (" + generatePlaceholders(searchQuery.fields.size()) + ")";
            for (const QString& field : searchQuery.fields) {
                params.append(field);
            }
        }
        
        sql += R"(
            GROUP BY st.entry_id, pe.source_line_number
            ORDER BY match_count DESC
        )";
        
        if (searchQuery.maxResults > 0) {
            sql += QString(" LIMIT %1").arg(searchQuery.maxResults);
        }

        params.prepend(searchQuery.query);
    }

    return executeSearchQuery(sql, params);
}

QList<SearchResult> DatabaseManager::executeSearchQuery(const QString& sql, const QVariantList& params)
{
    QList<SearchResult> results;
    QSqlQuery query = executeQuery(sql, params);
    
    if (query.lastError().isValid()) {
        return results;
    }

    while (query.next()) {
        SearchResult result;
        result.entryId = query.value(0).toInt();
        result.sourceLineNumber = query.value(1).toInt();
        result.relevanceScore = query.value(2).toFloat();
        result.matchedFields = query.value(3).toString().split(',', Qt::SkipEmptyParts);
        results.append(result);
    }

    return results;
}

bool DatabaseManager::updateSearchIndex(int entryId, const QString& fieldName, const QString& content)
{
    // First, remove existing tokens for this entry/field combination
    if (!executeNonQuery("DELETE FROM search_tokens WHERE entry_id = ? AND field_name = ?", 
                        {entryId, fieldName})) {
        return false;
    }

    // Tokenize the content and insert new tokens
    QStringList tokens = content.split(QRegularExpression("\\W+"), Qt::SkipEmptyParts);
    
    if (!prepareBulkInsert("search_tokens", {"entry_id", "field_name", "token", "position"})) {
        return false;
    }

    for (int i = 0; i < tokens.size(); ++i) {
        QString token = tokens[i].toLower(); // Store tokens in lowercase for case-insensitive search
        if (!executeBulkInsert({entryId, fieldName, token, i})) {
            finalizeBulkInsert();
            return false;
        }
    }

    return finalizeBulkInsert();
}

bool DatabaseManager::rebuildSearchIndex(const QString& tableName)
{
    // This would need to be implemented by the calling code since it needs to know
    // the structure of the parsed_entries table and how to extract text content
    Q_UNUSED(tableName);
    setError("rebuildSearchIndex must be implemented by the calling plugin");
    return false;
}

QString DatabaseManager::escapeString(const QString& str)
{
    QString escaped = str;
    escaped.replace("'", "''");
    return escaped;
}

QString DatabaseManager::generatePlaceholders(int count)
{
    QStringList placeholders;
    for (int i = 0; i < count; ++i) {
        placeholders.append("?");
    }
    return placeholders.join(", ");
}

bool DatabaseManager::tableExists(const QString& tableName)
{
    QVariant result = executeScalar(
        "SELECT name FROM sqlite_master WHERE type='table' AND name=?", 
        {tableName}
    );
    return !result.isNull();
}

QStringList DatabaseManager::getTableColumns(const QString& tableName)
{
    QStringList columns;
    QSqlQuery query = executeQuery(QString("PRAGMA table_info(%1)").arg(tableName));
    
    while (query.next()) {
        columns.append(query.value(1).toString()); // Column name is at index 1
    }

    return columns;
}

void DatabaseManager::handleDatabaseError(const QSqlError& error)
{
    if (error.isValid()) {
        setError(error.text());
        qDebug() << "Database error:" << error.text();
    }
}

void DatabaseManager::setStatus(DatabaseStatus status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged(status);
    }
}

void DatabaseManager::setError(const QString& error)
{
    m_lastError = error;
    setStatus(DatabaseStatus::Error);
    emit errorOccurred(error);
}

QString DatabaseManager::generateConnectionName()
{
    return QString("LogdorDB_%1_%2")
               .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
               .arg(++s_connectionCounter);
}
