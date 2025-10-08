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
    
    // Query operations
    QList<QVariantList> queryData(const QString& pluginName, const FilterOptions& filter);
    QSet<int> getFilteredLineNumbers(const QString& pluginName, const FilterOptions& filter);
    
    // Schema management
    bool updatePluginSchema(const QString& pluginName, const QList<FieldInfo>& newSchema);
    QList<FieldInfo> getPluginSchema(const QString& pluginName);
    bool pluginTableExists(const QString& pluginName);
    int getPluginSchemaVersion(const QString& pluginName);
    bool migratePluginSchema(const QString& pluginName, int fromVersion, int toVersion, const QList<FieldInfo>& newSchema);
    
    // Database information
    QString getDatabasePath() const { return m_databasePath; }
    QStringList getPluginTables() const;

signals:
    void statusChanged(PluginDatabaseStatus status);
    void errorOccurred(const QString& error);

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
};

#endif // PLUGINDATABASEMANAGER_H