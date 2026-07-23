#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>
#include <QStringList>

enum class DatabaseStatus {
    NotInitialized,
    Initializing,
    Ready,
    Error
};

struct SearchQuery {
    QString query;
    Qt::CaseSensitivity caseSensitivity;
    bool fuzzyMatch = true;
    QStringList fields; // Empty means all fields
    int maxResults = -1; // -1 means no limit
};

struct SearchResult {
    int entryId;
    int sourceLineNumber;
    float relevanceScore;
    QStringList matchedFields;
    QStringList snippets;
};

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(const QString& databasePath, QObject* parent = nullptr);
    ~DatabaseManager();

    // Database initialization and management
    bool createDatabase(const QString& schema);
    bool openDatabase();
    void closeDatabase();
    DatabaseStatus status() const { return m_status; }
    QString lastError() const { return m_lastError; }

    // Schema management
    bool createTables(const QString& schema);
    bool migrateSchema(int fromVersion, int toVersion, const QStringList& migrationCommands);
    int getSchemaVersion();
    bool updateSchemaVersion(int version);

    // Query execution
    QSqlQuery executeQuery(const QString& sql, const QVariantList& params = QVariantList());
    bool executeNonQuery(const QString& sql, const QVariantList& params = QVariantList());
    QVariantList executeScalarList(const QString& sql, const QVariantList& params = QVariantList());
    QVariant executeScalar(const QString& sql, const QVariantList& params = QVariantList());

    // Transaction management
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    
    // Bulk operations
    bool prepareBulkInsert(const QString& tableName, const QStringList& columns);
    bool executeBulkInsert(const QVariantList& values);
    bool finalizeBulkInsert();

    // Search operations
    QList<SearchResult> performSearch(const SearchQuery& searchQuery, const QString& tableName);
    bool updateSearchIndex(int entryId, const QString& fieldName, const QString& content);
    bool rebuildSearchIndex(const QString& tableName);

    // Utility functions
    QString escapeString(const QString& str);
    QString generatePlaceholders(int count);
    bool tableExists(const QString& tableName);
    QStringList getTableColumns(const QString& tableName);

signals:
    void statusChanged(DatabaseStatus status);
    void errorOccurred(const QString& error);

private slots:
    void handleDatabaseError(const QSqlError& error);

private:
    void setStatus(DatabaseStatus status);
    void setError(const QString& error);
    QString generateConnectionName();
    bool createSearchTables();
    QList<SearchResult> executeSearchQuery(const QString& sql, const QVariantList& params);

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
    DatabaseStatus m_status;
    QString m_lastError;
    QRecursiveMutex m_mutex; // recursive: openDatabase() re-enters executeQuery()
    
    // Bulk insert state
    QSqlQuery* m_bulkQuery;
    QString m_bulkTableName;
    QStringList m_bulkColumns;

    static int s_connectionCounter;
};

#endif // DATABASEMANAGER_H
