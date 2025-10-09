#ifndef PLUGINDATABASEMANAGER_H
#define PLUGINDATABASEMANAGER_H

#include "databasemanager.h"
#include "plugininterface.h"
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QSet>
#include <QMutex>
#include <QStringList>
#include <QDateTime>

enum class PluginDatabaseStatus {
    NotInitialized,
    Initializing,
    Ready,
    Error
};

struct DatabaseFieldInfo {
    QString name;
    DataType type;
    bool indexed;
    bool nullable;
    QVariant defaultValue;
    QString sqlType; // Generated SQL type string
    
    DatabaseFieldInfo(const QString& fieldName = QString(), 
                     DataType fieldType = DataType::String, 
                     bool isIndexed = false, 
                     bool isNullable = true, 
                     const QVariant& defValue = QVariant())
        : name(fieldName)
        , type(fieldType)
        , indexed(isIndexed)
        , nullable(isNullable)
        , defaultValue(defValue)
    {
        sqlType = generateSqlType();
    }
    
    QString generateSqlType() const;
};

struct PluginDataRecord {
    int id;
    int originalLineNumber;
    QDateTime createdAt;
    QVariantMap data; // Plugin-specific data
};

class PluginQueryBuilder {
public:
    PluginQueryBuilder();
    
    // Query building methods
    PluginQueryBuilder& where(const QString& field, const QVariant& value);
    PluginQueryBuilder& whereLike(const QString& field, const QString& pattern, Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive);
    PluginQueryBuilder& whereRegex(const QString& field, const QString& pattern, Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive);
    PluginQueryBuilder& whereNot(const QString& field, const QVariant& value);
    PluginQueryBuilder& andCondition();
    PluginQueryBuilder& orCondition();
    PluginQueryBuilder& openGroup();
    PluginQueryBuilder& closeGroup();
    PluginQueryBuilder& orderBy(const QString& field, Qt::SortOrder order = Qt::AscendingOrder);
    PluginQueryBuilder& limit(int count);
    PluginQueryBuilder& offset(int count);
    
    // Build final query
    QString buildQuery(const QString& tableName, const QStringList& selectFields = QStringList()) const;
    QString buildCountQuery(const QString& tableName) const;
    QVariantList getParameters() const;
    
    // Reset builder
    void reset();
    
    // Static helper for FilterOptions conversion
    static PluginQueryBuilder fromFilterOptions(const FilterOptions& filter, const QList<FieldInfo>& schema);

private:
    struct QueryCondition {
        enum Type { Where, WhereLike, WhereRegex, WhereNot, And, Or, OpenGroup, CloseGroup };
        Type type;
        QString field;
        QVariant value;
        Qt::CaseSensitivity caseSensitivity;
    };
    
    QList<QueryCondition> m_conditions;
    QString m_orderByField;
    Qt::SortOrder m_orderDirection;
    int m_limitCount;
    int m_offsetCount;
    QVariantList m_parameters;
    
    QString buildWhereClause() const;
    void addParameter(const QVariant& param);
};

class PluginDatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit PluginDatabaseManager(QObject* parent = nullptr);
    ~PluginDatabaseManager();

    // Database lifecycle
    bool initializeForFile(const QString& filePath);
    bool createPluginTable(const QString& pluginName, const QList<FieldInfo>& schema);
    void closeDatabase();
    
    // Status and error handling
    PluginDatabaseStatus status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    bool isReady() const { return m_status == PluginDatabaseStatus::Ready; }
    
    // Data operations
    bool insertParsedData(const QString& pluginName, const QVariantList& data, int originalLineNumber);
    bool insertBatchData(const QString& pluginName, const QList<QVariantList>& batchData, const QList<int>& lineNumbers);
    bool insertLargeBatchData(const QString& pluginName, const QList<QVariantList>& batchData, const QList<int>& lineNumbers, int chunkSize = 1000);
    
    // Query operations
    QList<QVariantList> queryData(const QString& pluginName, const FilterOptions& filter);
    QList<QVariantList> queryDataWithPagination(const QString& pluginName, const FilterOptions& filter, int limit, int offset = 0);
    QSet<int> getFilteredLineNumbers(const QString& pluginName, const FilterOptions& filter);
    int getFilteredRowCount(const QString& pluginName, const FilterOptions& filter);
    
    // Result metadata
    struct QueryResult {
        QList<QVariantList> data;
        QSet<int> lineNumbers;
        int totalCount;
        bool hasMore;
    };
    QueryResult queryDataWithMetadata(const QString& pluginName, const FilterOptions& filter, int limit = -1, int offset = 0);
    
    // Schema management
    bool updatePluginSchema(const QString& pluginName, const QList<FieldInfo>& newSchema);
    QList<FieldInfo> getPluginSchema(const QString& pluginName);
    bool pluginTableExists(const QString& pluginName);
    int getPluginSchemaVersion(const QString& pluginName);
    bool migratePluginSchema(const QString& pluginName, int fromVersion, int toVersion, const QList<FieldInfo>& newSchema);
    
    // Database information
    QString getDatabasePath() const { return m_databasePath; }
    QStringList getPluginTables() const;
    
    // Query optimization
    bool createIndexForField(const QString& pluginName, const QString& fieldName);
    bool dropIndexForField(const QString& pluginName, const QString& fieldName);
    QStringList getIndexesForPlugin(const QString& pluginName);
    
    // Error handling and fallback
    bool hasFallbackMode() const { return m_fallbackMode; }
    void enableFallbackMode(const QString& reason);
    void disableFallbackMode();
    bool checkDatabaseIntegrity();
    bool repairDatabase();
    bool rebuildDatabaseFromFile(const QString& filePath);
    
    // Database maintenance
    bool optimizeDatabase();
    bool vacuumDatabase();
    qint64 getDatabaseSize() const;
    QString getDatabaseVersion() const;
    bool performMaintenanceCheck();
    void schedulePeriodicMaintenance();

signals:
    void statusChanged(PluginDatabaseStatus status);
    void errorOccurred(const QString& error);
    void fallbackModeEnabled(const QString& reason);
    void fallbackModeDisabled();
    void databaseRepaired();
    void databaseRebuilt();

private slots:
    void handleDatabaseError(const QString& error);
    void handleDatabaseStatusChanged(DatabaseStatus status);

private:
    // Internal helper methods
    void setStatus(PluginDatabaseStatus status);
    void setError(const QString& error);
    QString generateDatabasePath(const QString& filePath);
    QString getPluginTableName(const QString& pluginName);
    bool createMetadataTables();
    bool savePluginSchema(const QString& pluginName, const QList<FieldInfo>& schema);
    QList<DatabaseFieldInfo> convertToDbFieldInfo(const QList<FieldInfo>& fieldInfo);
    QString generateCreateTableSql(const QString& tableName, const QList<DatabaseFieldInfo>& fields);
    QString generateInsertSql(const QString& tableName, const QList<DatabaseFieldInfo>& fields);
    bool createIndexes(const QString& tableName, const QList<DatabaseFieldInfo>& fields);
    bool performSchemaBackup(const QString& pluginName);
    bool restoreSchemaBackup(const QString& pluginName);
    QStringList generateMigrationCommands(const QString& tableName, const QList<DatabaseFieldInfo>& oldFields, const QList<DatabaseFieldInfo>& newFields);
    QVariant convertToSqlType(const QVariant& value, DataType targetType, const QString& fieldName);
    
    // Error handling and recovery helpers
    bool validateDatabaseOperation();
    bool handleDatabaseCorruption(const QString& operation);
    void recordFileMetadata(const QString& filePath);
    
    // Member variables
    QString m_databasePath;
    QString m_currentFilePath;
    PluginDatabaseStatus m_status;
    QString m_lastError;
    QMutex m_mutex;
    
    // Database manager instance
    DatabaseManager* m_dbManager;
    
    // Schema cache
    QMap<QString, QList<FieldInfo>> m_schemaCache;
    
    // Fallback and error handling
    bool m_fallbackMode;
    QString m_fallbackReason;
    int m_consecutiveErrors;
    static const int MAX_CONSECUTIVE_ERRORS = 3;
};

#endif // PLUGINDATABASEMANAGER_H