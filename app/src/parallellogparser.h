#ifndef PARALLELLOGPARSER_H
#define PARALLELLOGPARSER_H

#include "plugininterface.h"
#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QAtomicInt>
#include <QTimer>
#include <QTime>
#include <QElapsedTimer>
#include <memory>

// Forward declarations
class PluginDatabaseManager;
class DatabaseConnectionPool;
class DatabaseBatchInserter;

struct FileChunk {
    qint64 startOffset;
    qint64 endOffset;
    qint64 actualEndOffset; // Adjusted to line boundary
    int startLineNumber;
    int endLineNumber;
    QByteArray data;
    
    FileChunk(qint64 start = 0, qint64 end = 0, int startLine = 0)
        : startOffset(start)
        , endOffset(end)
        , actualEndOffset(end)
        , startLineNumber(startLine)
        , endLineNumber(startLine)
    {}
    
    bool isValid() const { return startOffset < actualEndOffset && !data.isEmpty(); }
    qint64 size() const { return actualEndOffset - startOffset; }
};

struct ParsedChunkResult {
    int chunkIndex;
    QList<QVariantList> parsedData;
    QList<int> lineNumbers;
    QString errorMessage;
    bool success;
    
    ParsedChunkResult(int index = -1)
        : chunkIndex(index)
        , success(false)
    {}
};

struct ParseResult {
    bool success;
    QString errorMessage;
    int totalLinesProcessed;
    int totalRecordsInserted;
    QElapsedTimer processingTime;
    
    ParseResult()
        : success(false)
        , totalLinesProcessed(0)
        , totalRecordsInserted(0)
    {}
};

class ChunkParseTask : public QObject, public QRunnable
{
    Q_OBJECT

public:
    ChunkParseTask(const FileChunk& chunk, 
                   PluginInterface* plugin,
                   const QString& pluginName,
                   QObject* parent = nullptr);
    
    void run() override;
    
    ParsedChunkResult getResult() const { return m_result; }

signals:
    void chunkCompleted(const ParsedChunkResult& result);
    void chunkFailed(int chunkIndex, const QString& error);

private:
    FileChunk m_chunk;
    PluginInterface* m_plugin;
    QString m_pluginName;
    ParsedChunkResult m_result;
    
    QList<LogEntry> parseChunkToLogEntries(const QByteArray& data, int startLineNumber);
};

class ParallelLogParser : public QObject
{
    Q_OBJECT

public:
    explicit ParallelLogParser(QObject* parent = nullptr);
    ~ParallelLogParser();

    // Configuration
    void setThreadCount(int threads);
    void setChunkSize(qint64 bytes);
    void setMaxMemoryUsage(qint64 bytes);
    
    int threadCount() const { return m_threadCount; }
    qint64 chunkSize() const { return m_chunkSize; }
    qint64 maxMemoryUsage() const { return m_maxMemoryUsage; }
    
    // Parsing operations
    QFuture<ParseResult> parseFileAsync(const QString& filePath, 
                                       PluginInterface* plugin,
                                       PluginDatabaseManager* dbManager);
    void cancelParsing();
    bool isParsing() const { return m_isParsing; }
    
    // Progress tracking
    int progressPercentage() const;
    QString currentStatus() const;
    qint64 bytesProcessed() const { return m_bytesProcessed; }
    qint64 totalBytes() const { return m_totalBytes; }
    QTime estimatedTimeRemaining() const;
    
    // Performance metrics
    double averageChunksPerSecond() const;
    double averageBytesPerSecond() const;
    int completedChunks() const { return m_completedChunks; }
    int totalChunks() const { return m_totalChunks; }

signals:
    void progressChanged(int percentage);
    void statusChanged(const QString& status);
    void parsingCompleted(const ParseResult& result);
    void parsingCancelled();
    void parsingFailed(const QString& error);
    void chunkProcessed(int chunkIndex, int totalChunks);

private slots:
    void onChunkCompleted(const ParsedChunkResult& result);
    void onChunkFailed(int chunkIndex, const QString& error);
    void updateProgress();

private:
    // Core parsing logic
    bool initializeParsing(const QString& filePath, PluginInterface* plugin, PluginDatabaseManager* dbManager);
    QList<FileChunk> createFileChunks(const QString& filePath);
    FileChunk adjustChunkToLineBoundary(const QString& filePath, const FileChunk& chunk);
    void startChunkProcessing();
    void finalizeParsing();
    void cleanupParsing();
    
    // Thread management
    void initializeThreadPool();
    void shutdownThreadPool();
    bool waitForCompletion(int timeoutMs = 30000);
    
    // Progress and status management
    void updateStatus(const QString& status);
    void calculateProgress();
    void updatePerformanceMetrics();
    
    // Error handling
    void handleParsingError(const QString& error);
    void handleChunkError(int chunkIndex, const QString& error);
    
    // Memory management
    bool checkMemoryUsage();
    void optimizeMemoryUsage();
    
    // Configuration
    int m_threadCount;
    qint64 m_chunkSize;
    qint64 m_maxMemoryUsage;
    
    // Parsing state
    bool m_isParsing;
    bool m_isCancelled;
    QString m_currentFilePath;
    PluginInterface* m_currentPlugin;
    PluginDatabaseManager* m_currentDbManager;
    QString m_currentPluginName;
    
    // Progress tracking
    QAtomicInt m_completedChunks;
    QAtomicInt m_totalChunks;
    QAtomicInt m_failedChunks;
    qint64 m_bytesProcessed;
    qint64 m_totalBytes;
    QString m_currentStatus;
    
    // Performance metrics
    QElapsedTimer m_parsingTimer;
    QTimer* m_progressTimer;
    QTime m_startTime;
    QList<qint64> m_chunkCompletionTimes;
    
    // Thread management
    std::unique_ptr<QThreadPool> m_threadPool;
    QMutex m_resultMutex;
    QWaitCondition m_completionCondition;
    
    // Results collection
    QMap<int, ParsedChunkResult> m_chunkResults;
    QQueue<ParsedChunkResult> m_pendingResults;
    ParseResult m_finalResult;
    
    // Synchronization
    mutable QMutex m_progressMutex;
    mutable QMutex m_statusMutex;
    
    // Database coordination
    std::unique_ptr<DatabaseConnectionPool> m_connectionPool;
    std::unique_ptr<DatabaseBatchInserter> m_batchInserter;
    
    // Constants
    static const int DEFAULT_THREAD_COUNT;
    static const qint64 DEFAULT_CHUNK_SIZE;
    static const qint64 DEFAULT_MAX_MEMORY;
    static const int PROGRESS_UPDATE_INTERVAL_MS;
    static const int MAX_PENDING_CHUNKS;
};

#endif // PARALLELLOGPARSER_H