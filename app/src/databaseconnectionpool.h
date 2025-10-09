#ifndef DATABASECONNECTIONPOOL_H
#define DATABASECONNECTIONPOOL_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QThread>
#include <QTimer>
#include <QAtomicInt>
#include <QDateTime>
#include <QMap>
#include <QStringList>
#include <memory>

class DatabaseConnection;

struct ConnectionInfo {
    QString connectionName;
    QString databasePath;
    QThread* ownerThread;
    qint64 lastUsed;
    bool inUse;
    int useCount;
    
    ConnectionInfo()
        : ownerThread(nullptr)
        , lastUsed(0)
        , inUse(false)
        , useCount(0)
    {}
};

class DatabaseConnectionPool : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseConnectionPool(QObject* parent = nullptr);
    ~DatabaseConnectionPool();

    // Pool configuration
    void setMaxConnections(int maxConnections);
    void setConnectionTimeout(int timeoutMs);
    void setIdleTimeout(int timeoutMs);
    void setDatabasePath(const QString& path);
    
    int maxConnections() const { return m_maxConnections; }
    int connectionTimeout() const { return m_connectionTimeout; }
    int idleTimeout() const { return m_idleTimeout; }
    QString databasePath() const { return m_databasePath; }
    
    // Connection management
    QString acquireConnection(int timeoutMs = -1);
    void releaseConnection(const QString& connectionName);
    bool isConnectionValid(const QString& connectionName);
    
    // Pool status
    int activeConnections() const;
    int availableConnections() const;
    int totalConnections() const;
    QStringList getConnectionNames() const;
    
    // Pool maintenance
    void cleanupIdleConnections();
    void closeAllConnections();
    bool validateAllConnections();
    
    // Thread safety
    bool isThreadSafe() const { return true; }

signals:
    void connectionCreated(const QString& connectionName);
    void connectionDestroyed(const QString& connectionName);
    void connectionAcquired(const QString& connectionName);
    void connectionReleased(const QString& connectionName);
    void poolExhausted();
    void errorOccurred(const QString& error);

private slots:
    void performMaintenance();

private:
    // Connection creation and destruction
    QString createConnection();
    void destroyConnection(const QString& connectionName);
    bool initializeConnection(const QString& connectionName);
    
    // Pool management
    QString findAvailableConnection();
    void updateConnectionUsage(const QString& connectionName);
    bool shouldCreateNewConnection() const;
    void removeExpiredConnections();
    
    // Thread safety helpers
    QString generateConnectionName();
    bool isConnectionInCurrentThread(const QString& connectionName);
    
    // Error handling
    void handleConnectionError(const QString& connectionName, const QString& error);
    void setError(const QString& error);
    
    // Configuration
    int m_maxConnections;
    int m_connectionTimeout;
    int m_idleTimeout;
    QString m_databasePath;
    
    // Pool state
    QMap<QString, ConnectionInfo> m_connections;
    QQueue<QString> m_availableConnections;
    QAtomicInt m_connectionCounter;
    QString m_lastError;
    
    // Synchronization
    mutable QMutex m_poolMutex;
    QWaitCondition m_connectionAvailable;
    
    // Maintenance
    QTimer* m_maintenanceTimer;
    
    // Constants
    static const int DEFAULT_MAX_CONNECTIONS;
    static const int DEFAULT_CONNECTION_TIMEOUT_MS;
    static const int DEFAULT_IDLE_TIMEOUT_MS;
    static const int MAINTENANCE_INTERVAL_MS;
};

class DatabaseBatchInserter : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseBatchInserter(DatabaseConnectionPool* pool, QObject* parent = nullptr);
    ~DatabaseBatchInserter();

    // Configuration
    void setBatchSize(int size);
    void setMaxPendingBatches(int count);
    void setInsertTimeout(int timeoutMs);
    
    int batchSize() const { return m_batchSize; }
    int maxPendingBatches() const { return m_maxPendingBatches; }
    int insertTimeout() const { return m_insertTimeout; }
    
    // Batch operations
    bool insertBatch(const QString& pluginName, 
                    const QList<QVariantList>& data, 
                    const QList<int>& lineNumbers);
    
    bool insertBatchAsync(const QString& pluginName, 
                         const QList<QVariantList>& data, 
                         const QList<int>& lineNumbers);
    
    void flushPendingBatches();
    void cancelPendingBatches();
    
    // Status
    int pendingBatchCount() const;
    int completedBatchCount() const { return m_completedBatches; }
    int failedBatchCount() const { return m_failedBatches; }
    bool hasPendingBatches() const;
    
    // Error handling
    QString lastError() const;
    QStringList getAllErrors() const;
    void clearErrors();

signals:
    void batchInserted(const QString& pluginName, int recordCount);
    void batchFailed(const QString& pluginName, const QString& error);
    void allBatchesCompleted();
    void errorOccurred(const QString& error);

private slots:
    void processPendingBatches();

private:
    struct BatchData {
        QString pluginName;
        QList<QVariantList> data;
        QList<int> lineNumbers;
        qint64 timestamp;
        
        BatchData(const QString& plugin = QString())
            : pluginName(plugin)
            , timestamp(QDateTime::currentMSecsSinceEpoch())
        {}
    };
    
    // Batch processing
    bool executeBatch(const BatchData& batch);
    QString prepareBatchInsertSql(const QString& pluginName, int recordCount);
    bool bindBatchParameters(QSqlQuery& query, const QList<QVariantList>& data, const QList<int>& lineNumbers);
    
    // Queue management
    void enqueueBatch(const BatchData& batch);
    BatchData dequeueBatch();
    void clearBatchQueue();
    
    // Error handling
    void recordError(const QString& error);
    void handleBatchError(const QString& pluginName, const QString& error);
    
    // Configuration
    int m_batchSize;
    int m_maxPendingBatches;
    int m_insertTimeout;
    
    // State
    DatabaseConnectionPool* m_connectionPool;
    QQueue<BatchData> m_pendingBatches;
    QAtomicInt m_completedBatches;
    QAtomicInt m_failedBatches;
    QStringList m_errors;
    
    // Synchronization
    mutable QMutex m_batchMutex;
    QWaitCondition m_batchAvailable;
    QTimer* m_processingTimer;
    
    // Constants
    static const int DEFAULT_BATCH_SIZE;
    static const int DEFAULT_MAX_PENDING_BATCHES;
    static const int DEFAULT_INSERT_TIMEOUT_MS;
    static const int PROCESSING_INTERVAL_MS;
};

#endif // DATABASECONNECTIONPOOL_H