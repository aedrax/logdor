#include "parallellogparser.h"
#include "plugindatabasemanager.h"
#include "filechunker.h"
#include "databaseconnectionpool.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QApplication>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>

// Constants
const int ParallelLogParser::DEFAULT_THREAD_COUNT = QThread::idealThreadCount();
const qint64 ParallelLogParser::DEFAULT_CHUNK_SIZE = 1024 * 1024; // 1MB
const qint64 ParallelLogParser::DEFAULT_MAX_MEMORY = 512 * 1024 * 1024; // 512MB
const int ParallelLogParser::PROGRESS_UPDATE_INTERVAL_MS = 100;
const int ParallelLogParser::MAX_PENDING_CHUNKS = 10;

ChunkParseTask::ChunkParseTask(const FileChunk& chunk, 
                               PluginInterface* plugin,
                               const QString& pluginName,
                               QObject* parent)
    : QObject(parent)
    , QRunnable()
    , m_chunk(chunk)
    , m_plugin(plugin)
    , m_pluginName(pluginName)
    , m_result(chunk.startLineNumber)
{
    setAutoDelete(false); // We'll manage deletion manually
}

void ChunkParseTask::run()
{
    if (!m_plugin || !m_chunk.isValid()) {
        m_result.success = false;
        m_result.errorMessage = "Invalid chunk or plugin";
        emit chunkFailed(m_result.chunkIndex, m_result.errorMessage);
        return;
    }

    try {
        // Parse chunk data into LogEntry objects
        QList<LogEntry> logEntries = parseChunkToLogEntries(m_chunk.data, m_chunk.startLineNumber);
        
        // Convert each LogEntry to database record if plugin supports it
        if (m_plugin->supportsDatabaseStorage()) {
            m_result.parsedData.reserve(logEntries.size());
            m_result.lineNumbers.reserve(logEntries.size());
            
            for (int i = 0; i < logEntries.size(); ++i) {
                const LogEntry& entry = logEntries[i];
                int lineNumber = m_chunk.startLineNumber + i;
                
                QVariantList record = m_plugin->parseToDatabaseRecord(entry, lineNumber);
                if (!record.isEmpty()) {
                    m_result.parsedData.append(record);
                    m_result.lineNumbers.append(lineNumber);
                }
            }
        }
        
        m_result.success = true;
        emit chunkCompleted(m_result);
        
    } catch (const std::exception& e) {
        m_result.success = false;
        m_result.errorMessage = QString("Exception during chunk parsing: %1").arg(e.what());
        emit chunkFailed(m_result.chunkIndex, m_result.errorMessage);
    } catch (...) {
        m_result.success = false;
        m_result.errorMessage = "Unknown exception during chunk parsing";
        emit chunkFailed(m_result.chunkIndex, m_result.errorMessage);
    }
}

QList<LogEntry> ChunkParseTask::parseChunkToLogEntries(const QByteArray& data, int startLineNumber)
{
    QList<LogEntry> entries;
    
    if (data.isEmpty()) {
        return entries;
    }
    
    // Split data into lines
    const char* dataPtr = data.constData();
    int dataSize = data.size();
    int lineStart = 0;
    
    for (int i = 0; i < dataSize; ++i) {
        if (dataPtr[i] == '\n') {
            // Found end of line
            int lineLength = i - lineStart;
            if (lineLength > 0) {
                // Create LogEntry pointing to the line data
                LogEntry entry(dataPtr + lineStart, lineLength);
                entries.append(entry);
            }
            lineStart = i + 1;
        }
    }
    
    // Handle last line if it doesn't end with newline
    if (lineStart < dataSize) {
        int lineLength = dataSize - lineStart;
        LogEntry entry(dataPtr + lineStart, lineLength);
        entries.append(entry);
    }
    
    return entries;
}

ParallelLogParser::ParallelLogParser(QObject* parent)
    : QObject(parent)
    , m_threadCount(DEFAULT_THREAD_COUNT)
    , m_chunkSize(DEFAULT_CHUNK_SIZE)
    , m_maxMemoryUsage(DEFAULT_MAX_MEMORY)
    , m_isParsing(false)
    , m_isCancelled(false)
    , m_currentPlugin(nullptr)
    , m_currentDbManager(nullptr)
    , m_completedChunks(0)
    , m_totalChunks(0)
    , m_failedChunks(0)
    , m_bytesProcessed(0)
    , m_totalBytes(0)
    , m_progressTimer(new QTimer(this))
{
    // Initialize progress timer
    m_progressTimer->setInterval(PROGRESS_UPDATE_INTERVAL_MS);
    connect(m_progressTimer, &QTimer::timeout, this, &ParallelLogParser::updateProgress);
    
    // Initialize thread pool
    initializeThreadPool();
    
    // Initialize database coordination components
    m_connectionPool = std::make_unique<DatabaseConnectionPool>(this);
    m_batchInserter = std::make_unique<DatabaseBatchInserter>(m_connectionPool.get(), this);
    
    // Configure connection pool for parallel access
    m_connectionPool->setMaxConnections(m_threadCount * 2); // Allow more connections than threads
    
    // Connect batch inserter signals
    connect(m_batchInserter.get(), &DatabaseBatchInserter::batchInserted,
            this, [this](const QString& pluginName, int recordCount) {
                Q_UNUSED(pluginName)
                m_finalResult.totalRecordsInserted += recordCount;
            });
    
    connect(m_batchInserter.get(), &DatabaseBatchInserter::batchFailed,
            this, [this](const QString& pluginName, const QString& error) {
                qWarning() << "Batch insertion failed for" << pluginName << ":" << error;
            });
}

ParallelLogParser::~ParallelLogParser()
{
    if (m_isParsing) {
        cancelParsing();
    }
    shutdownThreadPool();
}

void ParallelLogParser::setThreadCount(int threads)
{
    if (threads > 0 && threads <= QThread::idealThreadCount() * 2) {
        m_threadCount = threads;
        if (m_threadPool) {
            m_threadPool->setMaxThreadCount(threads);
        }
    }
}

void ParallelLogParser::setChunkSize(qint64 bytes)
{
    if (bytes > 0 && bytes <= 100 * 1024 * 1024) { // Max 100MB per chunk
        m_chunkSize = bytes;
    }
}

void ParallelLogParser::setMaxMemoryUsage(qint64 bytes)
{
    if (bytes > 0) {
        m_maxMemoryUsage = bytes;
    }
}

QFuture<ParseResult> ParallelLogParser::parseFileAsync(const QString& filePath, 
                                                      PluginInterface* plugin,
                                                      PluginDatabaseManager* dbManager)
{
    if (m_isParsing) {
        return QtConcurrent::run([this]() {
            ParseResult result;
            result.success = false;
            result.errorMessage = "Parser is already running";
            return result;
        });
    }
    
    return QtConcurrent::run([this, filePath, plugin, dbManager]() {
        ParseResult result;
        
        if (!initializeParsing(filePath, plugin, dbManager)) {
            result.success = false;
            result.errorMessage = m_finalResult.errorMessage;
            return result;
        }
        
        try {
            m_parsingTimer.start();
            m_progressTimer->start();
            
            startChunkProcessing();
            
            // Wait for completion or cancellation
            if (waitForCompletion()) {
                finalizeParsing();
                result = m_finalResult;
            } else {
                result.success = false;
                result.errorMessage = "Parsing timed out or was cancelled";
            }
            
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = QString("Exception during parsing: %1").arg(e.what());
        } catch (...) {
            result.success = false;
            result.errorMessage = "Unknown exception during parsing";
        }
        
        cleanupParsing();
        return result;
    });
}

void ParallelLogParser::cancelParsing()
{
    if (!m_isParsing) {
        return;
    }
    
    m_isCancelled = true;
    updateStatus("Cancelling parsing...");
    
    if (m_threadPool) {
        m_threadPool->clear(); // Remove pending tasks
        m_threadPool->waitForDone(5000); // Wait up to 5 seconds
    }
    
    m_progressTimer->stop();
    m_isParsing = false;
    
    emit parsingCancelled();
}

int ParallelLogParser::progressPercentage() const
{
    QMutexLocker locker(&m_progressMutex);
    if (m_totalChunks == 0) {
        return 0;
    }
    return (m_completedChunks * 100) / m_totalChunks;
}

QString ParallelLogParser::currentStatus() const
{
    QMutexLocker locker(&m_statusMutex);
    return m_currentStatus;
}

QTime ParallelLogParser::estimatedTimeRemaining() const
{
    if (m_completedChunks == 0 || m_totalChunks == 0) {
        return QTime();
    }
    
    qint64 elapsedMs = m_parsingTimer.elapsed();
    double completionRatio = static_cast<double>(m_completedChunks) / m_totalChunks;
    
    if (completionRatio > 0) {
        qint64 totalEstimatedMs = static_cast<qint64>(elapsedMs / completionRatio);
        qint64 remainingMs = totalEstimatedMs - elapsedMs;
        
        if (remainingMs > 0) {
            return QTime::fromMSecsSinceStartOfDay(static_cast<int>(remainingMs % (24 * 60 * 60 * 1000)));
        }
    }
    
    return QTime();
}

double ParallelLogParser::averageChunksPerSecond() const
{
    if (!m_parsingTimer.isValid() || m_completedChunks == 0) {
        return 0.0;
    }
    
    qint64 elapsedMs = m_parsingTimer.elapsed();
    if (elapsedMs == 0) {
        return 0.0;
    }
    
    return (static_cast<double>(m_completedChunks) * 1000.0) / elapsedMs;
}

double ParallelLogParser::averageBytesPerSecond() const
{
    if (!m_parsingTimer.isValid() || m_bytesProcessed == 0) {
        return 0.0;
    }
    
    qint64 elapsedMs = m_parsingTimer.elapsed();
    if (elapsedMs == 0) {
        return 0.0;
    }
    
    return (static_cast<double>(m_bytesProcessed) * 1000.0) / elapsedMs;
}

void ParallelLogParser::onChunkCompleted(const ParsedChunkResult& result)
{
    QMutexLocker locker(&m_resultMutex);
    
    if (m_isCancelled) {
        return;
    }
    
    m_chunkResults[result.chunkIndex] = result;
    m_completedChunks.fetchAndAddOrdered(1);
    m_bytesProcessed += m_chunkSize; // Approximate
    
    // Insert data into database using batch inserter for better coordination
    if (result.success && !result.parsedData.isEmpty()) {
        if (m_batchInserter && m_connectionPool) {
            // Use async batch insertion for better performance
            bool insertSuccess = m_batchInserter->insertBatchAsync(
                m_currentPluginName, 
                result.parsedData, 
                result.lineNumbers
            );
            
            if (!insertSuccess) {
                qWarning() << "Failed to queue batch data for chunk" << result.chunkIndex;
            }
        } else if (m_currentDbManager) {
            // Fallback to direct database manager insertion
            bool insertSuccess = m_currentDbManager->insertBatchData(
                m_currentPluginName, 
                result.parsedData, 
                result.lineNumbers
            );
            
            if (!insertSuccess) {
                qWarning() << "Failed to insert batch data for chunk" << result.chunkIndex;
            } else {
                m_finalResult.totalRecordsInserted += result.parsedData.size();
            }
        }
    }
    
    emit chunkProcessed(result.chunkIndex, m_totalChunks);
    
    // Check if all chunks are completed
    if (m_completedChunks + m_failedChunks >= m_totalChunks) {
        m_completionCondition.wakeAll();
    }
}

void ParallelLogParser::onChunkFailed(int chunkIndex, const QString& error)
{
    QMutexLocker locker(&m_resultMutex);
    
    if (m_isCancelled) {
        return;
    }
    
    m_failedChunks.fetchAndAddOrdered(1);
    qWarning() << "Chunk" << chunkIndex << "failed:" << error;
    
    // Check if all chunks are completed (including failed ones)
    if (m_completedChunks + m_failedChunks >= m_totalChunks) {
        m_completionCondition.wakeAll();
    }
}

void ParallelLogParser::updateProgress()
{
    calculateProgress();
    updatePerformanceMetrics();
    
    int percentage = progressPercentage();
    emit progressChanged(percentage);
    
    // Update status with performance info
    QString status = QString("Processing... %1% (%2/%3 chunks, %4 MB/s)")
                    .arg(percentage)
                    .arg(m_completedChunks.loadAcquire())
                    .arg(m_totalChunks.loadAcquire())
                    .arg(averageBytesPerSecond() / (1024 * 1024), 0, 'f', 1);
    
    updateStatus(status);
}

bool ParallelLogParser::initializeParsing(const QString& filePath, PluginInterface* plugin, PluginDatabaseManager* dbManager)
{
    if (!plugin) {
        m_finalResult.errorMessage = "Invalid plugin provided";
        return false;
    }
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        m_finalResult.errorMessage = QString("File does not exist or is not readable: %1").arg(filePath);
        return false;
    }
    
    m_currentFilePath = filePath;
    m_currentPlugin = plugin;
    m_currentDbManager = dbManager;
    m_currentPluginName = plugin->name();
    m_totalBytes = fileInfo.size();
    
    // Reset state
    m_isParsing = true;
    m_isCancelled = false;
    m_completedChunks = 0;
    m_failedChunks = 0;
    m_bytesProcessed = 0;
    m_chunkResults.clear();
    m_finalResult = ParseResult();
    
    // Configure database connection pool if database manager is available
    if (dbManager && m_connectionPool) {
        QString dbPath = dbManager->getDatabasePath();
        if (!dbPath.isEmpty()) {
            m_connectionPool->setDatabasePath(dbPath);
            updateStatus("Database connection pool configured");
        }
    }
    
    updateStatus("Initializing parallel parsing...");
    
    return true;
}

QList<FileChunk> ParallelLogParser::createFileChunks(const QString& filePath)
{
    QList<FileChunk> chunks;
    
    // Use FileChunker for improved chunking
    FileChunker chunker;
    chunker.setChunkSize(m_chunkSize);
    
    // Connect progress signals
    connect(&chunker, &FileChunker::chunkingProgress, this, [this](int progress) {
        updateStatus(QString("Creating chunks... %1%").arg(progress));
    });
    
    FileChunkingResult result = chunker.chunkFile(filePath);
    
    if (!result.success) {
        qWarning() << "File chunking failed:" << result.errorMessage;
        return chunks;
    }
    
    // Convert ChunkMetadata to FileChunk
    for (const ChunkMetadata& metadata : result.chunks) {
        FileChunk chunk;
        chunk.startOffset = metadata.fileOffset;
        chunk.endOffset = metadata.fileOffset + metadata.chunkSize;
        chunk.actualEndOffset = chunk.endOffset;
        chunk.startLineNumber = metadata.startLineNumber;
        chunk.endLineNumber = metadata.endLineNumber;
        
        // Read chunk data
        chunk.data = chunker.readChunkData(filePath, metadata);
        
        if (chunk.isValid()) {
            chunks.append(chunk);
        }
    }
    
    return chunks;
}

FileChunk ParallelLogParser::adjustChunkToLineBoundary(const QString& filePath, const FileChunk& chunk)
{
    // This method is now handled by FileChunker, but kept for compatibility
    return chunk;
}

void ParallelLogParser::startChunkProcessing()
{
    updateStatus("Creating file chunks...");
    
    QList<FileChunk> chunks = createFileChunks(m_currentFilePath);
    m_totalChunks = chunks.size();
    
    if (chunks.isEmpty()) {
        m_finalResult.success = false;
        m_finalResult.errorMessage = "No chunks created from file";
        return;
    }
    
    updateStatus(QString("Processing %1 chunks with %2 threads...").arg(chunks.size()).arg(m_threadCount));
    
    // Submit chunk processing tasks
    for (int i = 0; i < chunks.size(); ++i) {
        if (m_isCancelled) {
            break;
        }
        
        FileChunk& chunk = chunks[i];
        chunk.startLineNumber = i; // Use chunk index as identifier
        
        ChunkParseTask* task = new ChunkParseTask(chunk, m_currentPlugin, m_currentPluginName);
        
        // Connect signals
        connect(task, &ChunkParseTask::chunkCompleted, this, &ParallelLogParser::onChunkCompleted);
        connect(task, &ChunkParseTask::chunkFailed, this, &ParallelLogParser::onChunkFailed);
        
        m_threadPool->start(task);
    }
}

void ParallelLogParser::finalizeParsing()
{
    m_progressTimer->stop();
    
    // Flush any pending database batches
    if (m_batchInserter) {
        updateStatus("Flushing pending database batches...");
        m_batchInserter->flushPendingBatches();
        
        // Update final record count from batch inserter
        m_finalResult.totalRecordsInserted = m_batchInserter->completedBatchCount();
    }
    
    // Calculate final results
    m_finalResult.success = (m_failedChunks == 0);
    m_finalResult.totalLinesProcessed = m_completedChunks; // Approximate
    m_finalResult.processingTime = m_parsingTimer;
    
    if (m_finalResult.success) {
        updateStatus("Parsing completed successfully");
        emit parsingCompleted(m_finalResult);
    } else {
        m_finalResult.errorMessage = QString("Parsing completed with %1 failed chunks").arg(m_failedChunks.loadAcquire());
        updateStatus(m_finalResult.errorMessage);
        emit parsingFailed(m_finalResult.errorMessage);
    }
}

void ParallelLogParser::cleanupParsing()
{
    m_isParsing = false;
    m_currentPlugin = nullptr;
    m_currentDbManager = nullptr;
    m_currentFilePath.clear();
    m_currentPluginName.clear();
    
    // Clean up chunk results
    m_chunkResults.clear();
}

void ParallelLogParser::initializeThreadPool()
{
    m_threadPool = std::make_unique<QThreadPool>();
    m_threadPool->setMaxThreadCount(m_threadCount);
}

void ParallelLogParser::shutdownThreadPool()
{
    if (m_threadPool) {
        m_threadPool->clear();
        m_threadPool->waitForDone(10000); // Wait up to 10 seconds
        m_threadPool.reset();
    }
}

bool ParallelLogParser::waitForCompletion(int timeoutMs)
{
    QMutexLocker locker(&m_resultMutex);
    
    while (!m_isCancelled && (m_completedChunks + m_failedChunks < m_totalChunks)) {
        if (!m_completionCondition.wait(&m_resultMutex, timeoutMs)) {
            return false; // Timeout
        }
    }
    
    return !m_isCancelled;
}

void ParallelLogParser::updateStatus(const QString& status)
{
    QMutexLocker locker(&m_statusMutex);
    m_currentStatus = status;
    emit statusChanged(status);
}

void ParallelLogParser::calculateProgress()
{
    // Progress calculation is handled in progressPercentage()
    // This method can be extended for more complex progress calculations
}

void ParallelLogParser::updatePerformanceMetrics()
{
    // Record completion time for performance analysis
    if (m_parsingTimer.isValid()) {
        m_chunkCompletionTimes.append(m_parsingTimer.elapsed());
        
        // Keep only recent completion times (last 100)
        if (m_chunkCompletionTimes.size() > 100) {
            m_chunkCompletionTimes.removeFirst();
        }
    }
}

void ParallelLogParser::handleParsingError(const QString& error)
{
    m_finalResult.success = false;
    m_finalResult.errorMessage = error;
    updateStatus(QString("Parsing error: %1").arg(error));
    emit parsingFailed(error);
}

void ParallelLogParser::handleChunkError(int chunkIndex, const QString& error)
{
    qWarning() << "Chunk" << chunkIndex << "error:" << error;
    // Individual chunk errors are handled in onChunkFailed
}

bool ParallelLogParser::checkMemoryUsage()
{
    // This is a simplified memory check
    // In a real implementation, you might want to check actual memory usage
    qint64 estimatedMemoryUsage = m_chunkResults.size() * m_chunkSize;
    return estimatedMemoryUsage < m_maxMemoryUsage;
}

void ParallelLogParser::optimizeMemoryUsage()
{
    // Clear processed chunk results if memory usage is high
    if (!checkMemoryUsage()) {
        QMutexLocker locker(&m_resultMutex);
        
        // Keep only the most recent chunk results
        if (m_chunkResults.size() > MAX_PENDING_CHUNKS) {
            auto it = m_chunkResults.begin();
            while (m_chunkResults.size() > MAX_PENDING_CHUNKS && it != m_chunkResults.end()) {
                it = m_chunkResults.erase(it);
            }
        }
    }
}

