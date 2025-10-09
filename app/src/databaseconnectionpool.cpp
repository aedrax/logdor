#include "databaseconnectionpool.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMutexLocker>
#include <QDateTime>
#include <QCoreApplication>
#include <QUuid>

// Constants
const int DatabaseConnectionPool::DEFAULT_MAX_CONNECTIONS = 10;
const int DatabaseConnectionPool::DEFAULT_CONNECTION_TIMEOUT_MS = 5000;
const int DatabaseConnectionPool::DEFAULT_IDLE_TIMEOUT_MS = 300000; // 5 minutes
const int DatabaseConnectionPool::MAINTENANCE_INTERVAL_MS = 60000; // 1 minute

const int DatabaseBatchInserter::DEFAULT_BATCH_SIZE = 1000;
const int DatabaseBatchInserter::DEFAULT_MAX_PENDING_BATCHES = 50;
const int DatabaseBatchInserter::DEFAULT_INSERT_TIMEOUT_MS = 10000;
const int DatabaseBatchInserter::PROCESSING_INTERVAL_MS = 100;

DatabaseConnectionPool::DatabaseConnectionPool(QObject* parent)
    : QObject(parent)
    , m_maxConnections(DEFAULT_MAX_CONNECTIONS)
    , m_connectionTimeout(DEFAULT_CONNECTION_TIMEOUT_MS)
    , m_idleTimeout(DEFAULT_IDLE_TIMEOUT_MS)
    , m_connectionCounter(0)
    , m_maintenanceTimer(new QTimer(this))
{
    // Setup maintenance timer
    m_maintenanceTimer->setInterval(MAINTENANCE_INTERVAL_MS);
    connect(m_maintenanceTimer, &QTimer::timeout, this, &DatabaseConnectionPool::performMaintenance);
    m_maintenanceTimer->start();
}

DatabaseConnectionPool::~DatabaseConnectionPool()
{
    closeAllConnections();
}

void DatabaseConnectionPool::setMaxConnections(int maxConnections)
{
    if (maxConnections > 0 && maxConnections <= 100) {
        QMutexLocker locker(&m_poolMutex);
        m_maxConnections = maxConnections;
    }
}

void DatabaseConnectionPool::setConnectionTimeout(int timeoutMs)
{
    if (timeoutMs > 0) {
        m_connectionTimeout = timeoutMs;
    }
}

void DatabaseConnectionPool::setIdleTimeout(int timeoutMs)
{
    if (timeoutMs > 0) {
        m_idleTimeout = timeoutMs;
    }
}

void DatabaseConnectionPool::setDatabasePath(const QString& path)
{
    QMutexLocker locker(&m_poolMutex);
    if (m_databasePath != path) {
        // Close existing connections when path changes
        closeAllConnections();
        m_databasePath = path;
    }
}

QString DatabaseConnectionPool::acquireConnection(int timeoutMs)
{
    QMutexLocker locker(&m_poolMutex);
    
    if (m_databasePath.isEmpty()) {
        setError("Database path not set");
        return QString();
    }
    
    qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 
                     (timeoutMs > 0 ? timeoutMs : m_connectionTimeout);
    
    while (true) {
        // Try to find an available connection
        QString connectionName = findAvailableConnection();
        if (!connectionName.isEmpty()) {
            updateConnectionUsage(connectionName);
            emit connectionAcquired(connectionName);
            return connectionName;
        }
        
        // Create new connection if possible
        if (shouldCreateNewConnection()) {
            connectionName = createConnection();
            if (!connectionName.isEmpty()) {
                updateConnectionUsage(connectionName);
                emit connectionAcquired(connectionName);
                return connectionName;
            }
        }
        
        // Wait for a connection to become available
        qint64 remainingTime = deadline - QDateTime::currentMSecsSinceEpoch();
        if (remainingTime <= 0) {
            setError("Connection acquisition timeout");
            emit poolExhausted();
            return QString();
        }
        
        if (!m_connectionAvailable.wait(&m_poolMutex, static_cast<unsigned long>(remainingTime))) {
            setError("Connection acquisition timeout");
            emit poolExhausted();
            return QString();
        }
    }
}

void DatabaseConnectionPool::releaseConnection(const QString& connectionName)
{
    QMutexLocker locker(&m_poolMutex);
    
    auto it = m_connections.find(connectionName);
    if (it != m_connections.end()) {
        it->inUse = false;
        it->lastUsed = QDateTime::currentMSecsSinceEpoch();
        
        if (!m_availableConnections.contains(connectionName)) {
            m_availableConnections.enqueue(connectionName);
        }
        
        emit connectionReleased(connectionName);
        m_connectionAvailable.wakeOne();
    }
}

bool DatabaseConnectionPool::isConnectionValid(const QString& connectionName)
{
    QMutexLocker locker(&m_poolMutex);
    
    auto it = m_connections.find(connectionName);
    if (it == m_connections.end()) {
        return false;
    }
    
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    return db.isValid() && db.isOpen();
}

int DatabaseConnectionPool::activeConnections() const
{
    QMutexLocker locker(&m_poolMutex);
    int count = 0;
    for (const ConnectionInfo& info : m_connections) {
        if (info.inUse) {
            count++;
        }
    }
    return count;
}

int DatabaseConnectionPool::availableConnections() const
{
    QMutexLocker locker(&m_poolMutex);
    return m_availableConnections.size();
}

int DatabaseConnectionPool::totalConnections() const
{
    QMutexLocker locker(&m_poolMutex);
    return m_connections.size();
}

QStringList DatabaseConnectionPool::getConnectionNames() const
{
    QMutexLocker locker(&m_poolMutex);
    return m_connections.keys();
}

void DatabaseConnectionPool::cleanupIdleConnections()
{
    QMutexLocker locker(&m_poolMutex);
    removeExpiredConnections();
}

void DatabaseConnectionPool::closeAllConnections()
{
    QMutexLocker locker(&m_poolMutex);
    
    QStringList connectionNames = m_connections.keys();
    for (const QString& name : connectionNames) {
        destroyConnection(name);
    }
    
    m_connections.clear();
    m_availableConnections.clear();
}

bool DatabaseConnectionPool::validateAllConnections()
{
    QMutexLocker locker(&m_poolMutex);
    
    QStringList invalidConnections;
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (!isConnectionValid(it.key())) {
            invalidConnections.append(it.key());
        }
    }
    
    // Remove invalid connections
    for (const QString& name : invalidConnections) {
        destroyConnection(name);
    }
    
    return invalidConnections.isEmpty();
}

void DatabaseConnectionPool::performMaintenance()
{
    cleanupIdleConnections();
    validateAllConnections();
}

QString DatabaseConnectionPool::createConnection()
{
    QString connectionName = generateConnectionName();
    
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(m_databasePath);
    
    if (!db.open()) {
        setError(QString("Failed to open database connection: %1").arg(db.lastError().text()));
        QSqlDatabase::removeDatabase(connectionName);
        return QString();
    }
    
    if (!initializeConnection(connectionName)) {
        QSqlDatabase::removeDatabase(connectionName);
        return QString();
    }
    
    ConnectionInfo info;
    info.connectionName = connectionName;
    info.databasePath = m_databasePath;
    info.ownerThread = QThread::currentThread();
    info.lastUsed = QDateTime::currentMSecsSinceEpoch();
    info.inUse = false;
    info.useCount = 0;
    
    m_connections[connectionName] = info;
    m_availableConnections.enqueue(connectionName);
    
    emit connectionCreated(connectionName);
    return connectionName;
}

void DatabaseConnectionPool::destroyConnection(const QString& connectionName)
{
    auto it = m_connections.find(connectionName);
    if (it != m_connections.end()) {
        QSqlDatabase::removeDatabase(connectionName);
        m_connections.erase(it);
        
        // Remove from available queue
        QQueue<QString> newQueue;
        while (!m_availableConnections.isEmpty()) {
            QString name = m_availableConnections.dequeue();
            if (name != connectionName) {
                newQueue.enqueue(name);
            }
        }
        m_availableConnections = newQueue;
        
        emit connectionDestroyed(connectionName);
    }
}

bool DatabaseConnectionPool::initializeConnection(const QString& connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.isValid()) {
        return false;
    }
    
    // Set SQLite pragmas for performance and reliability
    QSqlQuery query(db);
    
    // Enable WAL mode for better concurrency
    if (!query.exec("PRAGMA journal_mode=WAL")) {
        qWarning() << "Failed to enable WAL mode:" << query.lastError().text();
    }
    
    // Set synchronous mode for better performance
    if (!query.exec("PRAGMA synchronous=NORMAL")) {
        qWarning() << "Failed to set synchronous mode:" << query.lastError().text();
    }
    
    // Set cache size
    if (!query.exec("PRAGMA cache_size=10000")) {
        qWarning() << "Failed to set cache size:" << query.lastError().text();
    }
    
    // Enable foreign keys
    if (!query.exec("PRAGMA foreign_keys=ON")) {
        qWarning() << "Failed to enable foreign keys:" << query.lastError().text();
    }
    
    return true;
}

QString DatabaseConnectionPool::findAvailableConnection()
{
    while (!m_availableConnections.isEmpty()) {
        QString connectionName = m_availableConnections.dequeue();
        
        auto it = m_connections.find(connectionName);
        if (it != m_connections.end() && !it->inUse && isConnectionValid(connectionName)) {
            return connectionName;
        }
        
        // Connection is invalid, remove it
        if (it != m_connections.end()) {
            destroyConnection(connectionName);
        }
    }
    
    return QString();
}

void DatabaseConnectionPool::updateConnectionUsage(const QString& connectionName)
{
    auto it = m_connections.find(connectionName);
    if (it != m_connections.end()) {
        it->inUse = true;
        it->lastUsed = QDateTime::currentMSecsSinceEpoch();
        it->useCount++;
    }
}

bool DatabaseConnectionPool::shouldCreateNewConnection() const
{
    return m_connections.size() < m_maxConnections;
}

void DatabaseConnectionPool::removeExpiredConnections()
{
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    QStringList expiredConnections;
    
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (!it->inUse && (currentTime - it->lastUsed) > m_idleTimeout) {
            expiredConnections.append(it.key());
        }
    }
    
    for (const QString& name : expiredConnections) {
        destroyConnection(name);
    }
}

QString DatabaseConnectionPool::generateConnectionName()
{
    return QString("pool_connection_%1_%2")
           .arg(QCoreApplication::applicationPid())
           .arg(m_connectionCounter.fetchAndAddOrdered(1));
}

bool DatabaseConnectionPool::isConnectionInCurrentThread(const QString& connectionName)
{
    auto it = m_connections.find(connectionName);
    return it != m_connections.end() && it->ownerThread == QThread::currentThread();
}

void DatabaseConnectionPool::handleConnectionError(const QString& connectionName, const QString& error)
{
    qWarning() << "Connection error for" << connectionName << ":" << error;
    
    // Remove the problematic connection
    destroyConnection(connectionName);
    
    emit errorOccurred(error);
}

void DatabaseConnectionPool::setError(const QString& error)
{
    m_lastError = error;
    qWarning() << "DatabaseConnectionPool error:" << error;
}

// DatabaseBatchInserter implementation

DatabaseBatchInserter::DatabaseBatchInserter(DatabaseConnectionPool* pool, QObject* parent)
    : QObject(parent)
    , m_batchSize(DEFAULT_BATCH_SIZE)
    , m_maxPendingBatches(DEFAULT_MAX_PENDING_BATCHES)
    , m_insertTimeout(DEFAULT_INSERT_TIMEOUT_MS)
    , m_connectionPool(pool)
    , m_completedBatches(0)
    , m_failedBatches(0)
    , m_processingTimer(new QTimer(this))
{
    if (!m_connectionPool) {
        qWarning() << "DatabaseBatchInserter created with null connection pool";
        return;
    }
    
    // Setup processing timer
    m_processingTimer->setInterval(PROCESSING_INTERVAL_MS);
    connect(m_processingTimer, &QTimer::timeout, this, &DatabaseBatchInserter::processPendingBatches);
    m_processingTimer->start();
}

DatabaseBatchInserter::~DatabaseBatchInserter()
{
    cancelPendingBatches();
}

void DatabaseBatchInserter::setBatchSize(int size)
{
    if (size > 0 && size <= 10000) {
        m_batchSize = size;
    }
}

void DatabaseBatchInserter::setMaxPendingBatches(int count)
{
    if (count > 0 && count <= 1000) {
        QMutexLocker locker(&m_batchMutex);
        m_maxPendingBatches = count;
    }
}

void DatabaseBatchInserter::setInsertTimeout(int timeoutMs)
{
    if (timeoutMs > 0) {
        m_insertTimeout = timeoutMs;
    }
}

bool DatabaseBatchInserter::insertBatch(const QString& pluginName, 
                                       const QList<QVariantList>& data, 
                                       const QList<int>& lineNumbers)
{
    if (!m_connectionPool || data.isEmpty()) {
        return false;
    }
    
    BatchData batch(pluginName);
    batch.data = data;
    batch.lineNumbers = lineNumbers;
    
    return executeBatch(batch);
}

bool DatabaseBatchInserter::insertBatchAsync(const QString& pluginName, 
                                            const QList<QVariantList>& data, 
                                            const QList<int>& lineNumbers)
{
    if (!m_connectionPool || data.isEmpty()) {
        return false;
    }
    
    QMutexLocker locker(&m_batchMutex);
    
    if (m_pendingBatches.size() >= m_maxPendingBatches) {
        recordError("Maximum pending batches exceeded");
        return false;
    }
    
    BatchData batch(pluginName);
    batch.data = data;
    batch.lineNumbers = lineNumbers;
    
    enqueueBatch(batch);
    return true;
}

void DatabaseBatchInserter::flushPendingBatches()
{
    QMutexLocker locker(&m_batchMutex);
    
    while (!m_pendingBatches.isEmpty()) {
        BatchData batch = dequeueBatch();
        locker.unlock();
        
        executeBatch(batch);
        
        locker.relock();
    }
    
    emit allBatchesCompleted();
}

void DatabaseBatchInserter::cancelPendingBatches()
{
    QMutexLocker locker(&m_batchMutex);
    clearBatchQueue();
}

int DatabaseBatchInserter::pendingBatchCount() const
{
    QMutexLocker locker(&m_batchMutex);
    return m_pendingBatches.size();
}

bool DatabaseBatchInserter::hasPendingBatches() const
{
    QMutexLocker locker(&m_batchMutex);
    return !m_pendingBatches.isEmpty();
}

QString DatabaseBatchInserter::lastError() const
{
    QMutexLocker locker(&m_batchMutex);
    return m_errors.isEmpty() ? QString() : m_errors.last();
}

QStringList DatabaseBatchInserter::getAllErrors() const
{
    QMutexLocker locker(&m_batchMutex);
    return m_errors;
}

void DatabaseBatchInserter::clearErrors()
{
    QMutexLocker locker(&m_batchMutex);
    m_errors.clear();
}

void DatabaseBatchInserter::processPendingBatches()
{
    QMutexLocker locker(&m_batchMutex);
    
    if (m_pendingBatches.isEmpty()) {
        return;
    }
    
    // Process one batch at a time to avoid blocking
    BatchData batch = dequeueBatch();
    locker.unlock();
    
    executeBatch(batch);
}

bool DatabaseBatchInserter::executeBatch(const BatchData& batch)
{
    if (!m_connectionPool) {
        handleBatchError(batch.pluginName, "No connection pool available");
        return false;
    }
    
    QString connectionName = m_connectionPool->acquireConnection(m_insertTimeout);
    if (connectionName.isEmpty()) {
        handleBatchError(batch.pluginName, "Failed to acquire database connection");
        return false;
    }
    
    bool success = false;
    
    try {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (!db.isValid() || !db.isOpen()) {
            handleBatchError(batch.pluginName, "Invalid database connection");
            m_connectionPool->releaseConnection(connectionName);
            return false;
        }
        
        // Start transaction
        if (!db.transaction()) {
            handleBatchError(batch.pluginName, QString("Failed to start transaction: %1").arg(db.lastError().text()));
            m_connectionPool->releaseConnection(connectionName);
            return false;
        }
        
        // Prepare batch insert SQL
        QString sql = prepareBatchInsertSql(batch.pluginName, batch.data.size());
        QSqlQuery query(db);
        
        if (!query.prepare(sql)) {
            db.rollback();
            handleBatchError(batch.pluginName, QString("Failed to prepare query: %1").arg(query.lastError().text()));
            m_connectionPool->releaseConnection(connectionName);
            return false;
        }
        
        // Bind parameters
        if (!bindBatchParameters(query, batch.data, batch.lineNumbers)) {
            db.rollback();
            handleBatchError(batch.pluginName, "Failed to bind batch parameters");
            m_connectionPool->releaseConnection(connectionName);
            return false;
        }
        
        // Execute batch insert
        if (!query.exec()) {
            db.rollback();
            handleBatchError(batch.pluginName, QString("Failed to execute batch insert: %1").arg(query.lastError().text()));
            m_connectionPool->releaseConnection(connectionName);
            return false;
        }
        
        // Commit transaction
        if (!db.commit()) {
            db.rollback();
            handleBatchError(batch.pluginName, QString("Failed to commit transaction: %1").arg(db.lastError().text()));
            m_connectionPool->releaseConnection(connectionName);
            return false;
        }
        
        success = true;
        m_completedBatches.fetchAndAddOrdered(1);
        emit batchInserted(batch.pluginName, batch.data.size());
        
    } catch (const std::exception& e) {
        handleBatchError(batch.pluginName, QString("Exception during batch insert: %1").arg(e.what()));
    } catch (...) {
        handleBatchError(batch.pluginName, "Unknown exception during batch insert");
    }
    
    m_connectionPool->releaseConnection(connectionName);
    return success;
}

QString DatabaseBatchInserter::prepareBatchInsertSql(const QString& pluginName, int recordCount)
{
    // This is a simplified implementation
    // In a real implementation, you would get the actual table schema
    QString tableName = QString("plugin_%1_data").arg(pluginName.toLower());
    
    // Basic insert statement - this should be customized based on actual schema
    QString sql = QString("INSERT INTO %1 (original_line_number, created_at").arg(tableName);
    
    // Add placeholder columns (this should be dynamic based on plugin schema)
    for (int i = 0; i < 5; ++i) { // Assume max 5 data columns for now
        sql += QString(", data_col_%1").arg(i);
    }
    
    sql += ") VALUES ";
    
    // Add value placeholders
    QStringList valuePlaceholders;
    for (int i = 0; i < recordCount; ++i) {
        valuePlaceholders.append("(?, CURRENT_TIMESTAMP, ?, ?, ?, ?, ?)");
    }
    
    sql += valuePlaceholders.join(", ");
    
    return sql;
}

bool DatabaseBatchInserter::bindBatchParameters(QSqlQuery& query, const QList<QVariantList>& data, const QList<int>& lineNumbers)
{
    int paramIndex = 0;
    
    for (int i = 0; i < data.size(); ++i) {
        // Bind line number
        query.bindValue(paramIndex++, lineNumbers.value(i, i + 1));
        
        // Bind data values
        const QVariantList& record = data[i];
        for (int j = 0; j < 5; ++j) { // Match the column count in prepareBatchInsertSql
            QVariant value = (j < record.size()) ? record[j] : QVariant();
            query.bindValue(paramIndex++, value);
        }
    }
    
    return true;
}

void DatabaseBatchInserter::enqueueBatch(const BatchData& batch)
{
    m_pendingBatches.enqueue(batch);
    m_batchAvailable.wakeOne();
}

DatabaseBatchInserter::BatchData DatabaseBatchInserter::dequeueBatch()
{
    if (m_pendingBatches.isEmpty()) {
        return BatchData();
    }
    
    return m_pendingBatches.dequeue();
}

void DatabaseBatchInserter::clearBatchQueue()
{
    m_pendingBatches.clear();
}

void DatabaseBatchInserter::recordError(const QString& error)
{
    m_errors.append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString()).arg(error));
    
    // Keep only recent errors (last 100)
    if (m_errors.size() > 100) {
        m_errors.removeFirst();
    }
    
    emit errorOccurred(error);
}

void DatabaseBatchInserter::handleBatchError(const QString& pluginName, const QString& error)
{
    m_failedBatches.fetchAndAddOrdered(1);
    recordError(error);
    emit batchFailed(pluginName, error);
}