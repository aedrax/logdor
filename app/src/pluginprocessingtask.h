#ifndef PLUGINPROCESSINGTASK_H
#define PLUGINPROCESSINGTASK_H

#include "backgroundtaskmanager.h"
#include "progresstracker.h"
#include "plugininterface.h"
#include <QObject>
#include <QString>
#include <QList>
#include <memory>

// Forward declarations
class PluginManager;
class PluginDatabaseManager;
class ParallelLogParser;
struct ParseResult;

// Plugin processing task for background log file analysis
class PluginProcessingTask : public BackgroundTask
{
    Q_OBJECT

public:
    explicit PluginProcessingTask(const TaskId& id,
                                 const QString& filePath,
                                 const QList<LogEntry>& logEntries,
                                 PluginManager* pluginManager,
                                 TaskPriority priority = TaskPriority::Normal,
                                 QObject* parent = nullptr);
    ~PluginProcessingTask();

    // Task execution
    void execute() override;
    void cancel() override;
    bool canCancel() const override { return true; }
    
    // Configuration
    void setUseParallelProcessing(bool useParallel);
    void setMaxThreads(int threads);
    void setChunkSize(qint64 bytes);
    
    // Progress information
    QString currentStage() const;
    QStringList getProcessingStages() const;
    
    // Results
    struct ProcessingResult {
        bool success;
        QString errorMessage;
        int totalLinesProcessed;
        int totalRecordsInserted;
        QElapsedTimer processingTime;
        QMap<QString, int> pluginRecordCounts;
        
        ProcessingResult() : success(false), totalLinesProcessed(0), totalRecordsInserted(0) {}
    };
    
    ProcessingResult getProcessingResult() const { return m_processingResult; }

signals:
    void stageChanged(const QString& stageName);
    void pluginProcessingStarted(const QString& pluginName);
    void pluginProcessingCompleted(const QString& pluginName, int recordCount);
    void pluginProcessingFailed(const QString& pluginName, const QString& error);

private slots:
    void onParsingProgressChanged(int percentage);
    void onParsingStatusChanged(const QString& status);
    void onParsingCompleted(const ParseResult& result);
    void onParsingCancelled();
    void onParsingFailed(const QString& error);

private:
    // Processing stages
    void initializeProcessing();
    void setupDatabaseForPlugins();
    void processLogEntriesSequential();
    void processLogEntriesParallel();
    void finalizeProcessing();
    void cleanupProcessing();
    
    // Plugin processing helpers
    bool processPluginData(PluginInterface* plugin);
    bool processPluginDataWithDatabase(PluginInterface* plugin);
    bool processPluginDataInMemory(PluginInterface* plugin);
    
    // Progress tracking helpers
    void setupProgressTracking();
    void updateStageProgress(const QString& stage, int percentage);
    void updateOverallProgress();
    
    // Error handling
    void handleProcessingError(const QString& error);
    void handlePluginError(const QString& pluginName, const QString& error);
    
    // Input data
    QString m_filePath;
    QList<LogEntry> m_logEntries;
    PluginManager* m_pluginManager;
    
    // Configuration
    bool m_useParallelProcessing;
    int m_maxThreads;
    qint64 m_chunkSize;
    
    // Processing state
    QString m_currentStage;
    QStringList m_processingStages;
    QMap<QString, int> m_stageProgress;
    
    // Progress tracking
    std::shared_ptr<MultiStageProgressTracker> m_multiStageTracker;
    
    // Parallel processing
    std::unique_ptr<ParallelLogParser> m_parallelParser;
    
    // Results
    ProcessingResult m_processingResult;
    
    // Plugin processing state
    QList<PluginInterface*> m_enabledPlugins;
    QMap<QString, bool> m_pluginDatabaseSupport;
    int m_currentPluginIndex;
    
    // Constants
    static const QStringList DEFAULT_PROCESSING_STAGES;
    static const int DEFAULT_MAX_THREADS;
    static const qint64 DEFAULT_CHUNK_SIZE;
};

// Factory class for creating plugin processing tasks
class PluginProcessingTaskFactory
{
public:
    // Create a standard plugin processing task
    static std::shared_ptr<PluginProcessingTask> createTask(
        const QString& filePath,
        const QList<LogEntry>& logEntries,
        PluginManager* pluginManager,
        TaskPriority priority = TaskPriority::Normal);
    
    // Create a task with custom configuration
    static std::shared_ptr<PluginProcessingTask> createConfiguredTask(
        const QString& filePath,
        const QList<LogEntry>& logEntries,
        PluginManager* pluginManager,
        bool useParallelProcessing,
        int maxThreads,
        qint64 chunkSize,
        TaskPriority priority = TaskPriority::Normal);
    
    // Create a task for large files (optimized settings)
    static std::shared_ptr<PluginProcessingTask> createLargeFileTask(
        const QString& filePath,
        const QList<LogEntry>& logEntries,
        PluginManager* pluginManager,
        TaskPriority priority = TaskPriority::High);

private:
    static TaskId generateTaskId(const QString& filePath);
    static void configureTaskForFileSize(PluginProcessingTask* task, qint64 fileSize);
};

#endif // PLUGINPROCESSINGTASK_H