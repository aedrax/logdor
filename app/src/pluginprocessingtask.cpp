#include "pluginprocessingtask.h"
#include "pluginmanager.h"
#include "plugindatabasemanager.h"
#include "parallellogparser.h"
#include <QDebug>
#include <QFileInfo>
#include <QThread>
#include <QUuid>

// Constants
const QStringList PluginProcessingTask::DEFAULT_PROCESSING_STAGES = {
    "Initializing",
    "Setting up database",
    "Processing log entries",
    "Finalizing"
};

const int PluginProcessingTask::DEFAULT_MAX_THREADS = QThread::idealThreadCount();
const qint64 PluginProcessingTask::DEFAULT_CHUNK_SIZE = 1024 * 1024; // 1MB

// PluginProcessingTask implementation
PluginProcessingTask::PluginProcessingTask(const TaskId& id,
                                         const QString& filePath,
                                         const QList<LogEntry>& logEntries,
                                         PluginManager* pluginManager,
                                         TaskPriority priority,
                                         QObject* parent)
    : BackgroundTask(id, priority, parent)
    , m_filePath(filePath)
    , m_logEntries(logEntries)
    , m_pluginManager(pluginManager)
    , m_useParallelProcessing(true)
    , m_maxThreads(DEFAULT_MAX_THREADS)
    , m_chunkSize(DEFAULT_CHUNK_SIZE)
    , m_processingStages(DEFAULT_PROCESSING_STAGES)
    , m_currentPluginIndex(0)
{
    setupProgressTracking();
}

PluginProcessingTask::~PluginProcessingTask()
{
    cleanupProcessing();
}

void PluginProcessingTask::execute()
{
    if (shouldCancel()) {
        return;
    }
    
    setStatus(TaskStatus::Running);
    emit started(taskId());
    
    m_processingResult.processingTime.start();
    
    try {
        initializeProcessing();
        
        if (shouldCancel()) return;
        setupDatabaseForPlugins();
        
        if (shouldCancel()) return;
        if (m_useParallelProcessing && !m_logEntries.isEmpty()) {
            processLogEntriesParallel();
        } else {
            processLogEntriesSequential();
        }
        
        if (shouldCancel()) return;
        finalizeProcessing();
        
        m_processingResult.success = true;
        setResult(QVariant::fromValue(m_processingResult));
        
    } catch (const std::exception& e) {
        handleProcessingError(QString("Processing failed: %1").arg(e.what()));
    } catch (...) {
        handleProcessingError("Processing failed with unknown error");
    }
    
    cleanupProcessing();
}

void PluginProcessingTask::cancel()
{
    BackgroundTask::cancel();
    
    if (m_parallelParser) {
        m_parallelParser->cancelParsing();
    }
    
    if (m_multiStageTracker) {
        m_multiStageTracker->cancel("User cancelled operation");
    }
}

void PluginProcessingTask::setUseParallelProcessing(bool useParallel)
{
    m_useParallelProcessing = useParallel;
}

void PluginProcessingTask::setMaxThreads(int threads)
{
    m_maxThreads = qMax(1, threads);
}

void PluginProcessingTask::setChunkSize(qint64 bytes)
{
    m_chunkSize = qMax(static_cast<qint64>(1024), bytes);
}

QString PluginProcessingTask::currentStage() const
{
    return m_currentStage;
}

QStringList PluginProcessingTask::getProcessingStages() const
{
    return m_processingStages;
}

void PluginProcessingTask::initializeProcessing()
{
    updateStageProgress("Initializing", 0);
    setProgress(5, "Initializing plugin processing...");
    
    if (!m_pluginManager) {
        throw std::runtime_error("Plugin manager is null");
    }
    
    // Get enabled plugins
    m_enabledPlugins = m_pluginManager->enabledPlugins();
    if (m_enabledPlugins.isEmpty()) {
        qWarning() << "No enabled plugins found for processing";
        return;
    }
    
    // Check database support for each plugin
    for (PluginInterface* plugin : m_enabledPlugins) {
        bool supportsDatabase = plugin->supportsDatabaseStorage();
        m_pluginDatabaseSupport[plugin->name()] = supportsDatabase;
        
        qDebug() << "Plugin" << plugin->name() << "database support:" << supportsDatabase;
    }
    
    updateStageProgress("Initializing", 100);
    setProgress(10, QString("Initialized processing for %1 plugins").arg(m_enabledPlugins.size()));
}

void PluginProcessingTask::setupDatabaseForPlugins()
{
    updateStageProgress("Setting up database", 0);
    setProgress(15, "Setting up database for plugins...");
    
    if (m_filePath.isEmpty()) {
        qDebug() << "No file path provided, skipping database setup";
        updateStageProgress("Setting up database", 100);
        setProgress(25, "Database setup skipped (no file path)");
        return;
    }
    
    // Initialize database for file if needed
    if (!m_pluginManager->isDatabaseInitializedForFile(m_filePath)) {
        qDebug() << "Initializing database for file:" << m_filePath;
        
        // This will be handled by the plugin manager when setLogs is called
        // For now, we just mark this stage as complete
    }
    
    updateStageProgress("Setting up database", 100);
    setProgress(25, "Database setup completed");
}

void PluginProcessingTask::processLogEntriesSequential()
{
    updateStageProgress("Processing log entries", 0);
    setProgress(30, "Processing log entries sequentially...");
    
    if (m_logEntries.isEmpty()) {
        qDebug() << "No log entries to process";
        updateStageProgress("Processing log entries", 100);
        setProgress(80, "No log entries to process");
        return;
    }
    
    // Set logs for all plugins first
    if (!m_pluginManager->setLogs(m_logEntries, m_filePath)) {
        throw std::runtime_error("Failed to set logs for plugins");
    }
    
    int totalPlugins = m_enabledPlugins.size();
    int processedPlugins = 0;
    
    // Process each plugin
    for (PluginInterface* plugin : m_enabledPlugins) {
        if (shouldCancel()) {
            return;
        }
        
        QString pluginName = plugin->name();
        emit pluginProcessingStarted(pluginName);
        
        setProgress(30 + (processedPlugins * 50) / totalPlugins, 
                   QString("Processing plugin: %1").arg(pluginName));
        
        try {
            if (processPluginData(plugin)) {
                int recordCount = m_processingResult.pluginRecordCounts.value(pluginName, 0);
                emit pluginProcessingCompleted(pluginName, recordCount);
                qDebug() << "Successfully processed plugin:" << pluginName << "with" << recordCount << "records";
            } else {
                QString error = QString("Failed to process plugin: %1").arg(pluginName);
                handlePluginError(pluginName, error);
                emit pluginProcessingFailed(pluginName, error);
            }
        } catch (const std::exception& e) {
            QString error = QString("Exception in plugin %1: %2").arg(pluginName, e.what());
            handlePluginError(pluginName, error);
            emit pluginProcessingFailed(pluginName, error);
        }
        
        processedPlugins++;
        updateStageProgress("Processing log entries", (processedPlugins * 100) / totalPlugins);
    }
    
    setProgress(80, QString("Completed processing %1 plugins").arg(processedPlugins));
}

void PluginProcessingTask::processLogEntriesParallel()
{
    updateStageProgress("Processing log entries", 0);
    setProgress(30, "Processing log entries in parallel...");
    
    if (m_logEntries.isEmpty()) {
        qDebug() << "No log entries to process";
        updateStageProgress("Processing log entries", 100);
        setProgress(80, "No log entries to process");
        return;
    }
    
    // For parallel processing, we need to use the ParallelLogParser
    // This is more complex and would require integration with database-capable plugins
    
    // For now, fall back to sequential processing
    qDebug() << "Parallel processing not fully implemented, falling back to sequential";
    processLogEntriesSequential();
}

void PluginProcessingTask::finalizeProcessing()
{
    updateStageProgress("Finalizing", 0);
    setProgress(85, "Finalizing processing...");
    
    // Calculate totals
    m_processingResult.totalLinesProcessed = m_logEntries.size();
    
    int totalRecords = 0;
    for (int count : m_processingResult.pluginRecordCounts) {
        totalRecords += count;
    }
    m_processingResult.totalRecordsInserted = totalRecords;
    
    updateStageProgress("Finalizing", 100);
    setProgress(100, QString("Processing completed: %1 lines, %2 records")
                    .arg(m_processingResult.totalLinesProcessed)
                    .arg(m_processingResult.totalRecordsInserted));
    
    qDebug() << "Plugin processing completed successfully";
    qDebug() << "Total lines processed:" << m_processingResult.totalLinesProcessed;
    qDebug() << "Total records inserted:" << m_processingResult.totalRecordsInserted;
    qDebug() << "Processing time:" << m_processingResult.processingTime.elapsed() << "ms";
}

void PluginProcessingTask::cleanupProcessing()
{
    if (m_parallelParser) {
        m_parallelParser.reset();
    }
    
    // Clear temporary data
    m_enabledPlugins.clear();
    m_pluginDatabaseSupport.clear();
    m_currentPluginIndex = 0;
}

bool PluginProcessingTask::processPluginData(PluginInterface* plugin)
{
    if (!plugin) {
        return false;
    }
    
    QString pluginName = plugin->name();
    
    // Check if plugin supports database storage
    if (m_pluginDatabaseSupport.value(pluginName, false)) {
        return processPluginDataWithDatabase(plugin);
    } else {
        return processPluginDataInMemory(plugin);
    }
}

bool PluginProcessingTask::processPluginDataWithDatabase(PluginInterface* plugin)
{
    QString pluginName = plugin->name();
    qDebug() << "Processing plugin with database support:" << pluginName;
    
    // Get database manager
    PluginDatabaseManager* dbManager = m_pluginManager->getDatabaseManager();
    if (!dbManager || !dbManager->isReady()) {
        qWarning() << "Database manager not available for plugin:" << pluginName;
        return false;
    }
    
    // Get plugin schema
    QList<FieldInfo> schema = plugin->getDatabaseSchema();
    if (schema.isEmpty()) {
        qWarning() << "Plugin" << pluginName << "returned empty database schema";
        return false;
    }
    
    // Create plugin table if needed
    if (!dbManager->createPluginTable(pluginName, schema)) {
        qWarning() << "Failed to create database table for plugin:" << pluginName;
        return false;
    }
    
    // Process log entries and insert into database
    QList<QVariantList> batchData;
    QList<int> lineNumbers;
    
    for (int i = 0; i < m_logEntries.size(); ++i) {
        if (shouldCancel()) {
            return false;
        }
        
        const LogEntry& entry = m_logEntries[i];
        
        // Parse entry to database record
        QVariantList record = plugin->parseToDatabaseRecord(entry, i + 1);
        if (!record.isEmpty()) {
            batchData.append(record);
            lineNumbers.append(i + 1);
        }
        
        // Insert in batches for better performance
        if (batchData.size() >= 1000) {
            if (!dbManager->insertBatchData(pluginName, batchData, lineNumbers)) {
                qWarning() << "Failed to insert batch data for plugin:" << pluginName;
                return false;
            }
            
            m_processingResult.pluginRecordCounts[pluginName] += batchData.size();
            batchData.clear();
            lineNumbers.clear();
        }
    }
    
    // Insert remaining data
    if (!batchData.isEmpty()) {
        if (!dbManager->insertBatchData(pluginName, batchData, lineNumbers)) {
            qWarning() << "Failed to insert final batch data for plugin:" << pluginName;
            return false;
        }
        
        m_processingResult.pluginRecordCounts[pluginName] += batchData.size();
    }
    
    qDebug() << "Successfully processed" << m_processingResult.pluginRecordCounts[pluginName] 
             << "records for plugin:" << pluginName;
    
    return true;
}

bool PluginProcessingTask::processPluginDataInMemory(PluginInterface* plugin)
{
    QString pluginName = plugin->name();
    qDebug() << "Processing plugin with in-memory storage:" << pluginName;
    
    // For in-memory plugins, the processing is already done by setLogs()
    // We just need to count the processed entries
    
    // This is a simplified approach - in reality, we might want to
    // get more detailed information from the plugin about processed records
    m_processingResult.pluginRecordCounts[pluginName] = m_logEntries.size();
    
    return true;
}

void PluginProcessingTask::setupProgressTracking()
{
    // Create multi-stage progress tracker
    m_multiStageTracker = std::make_shared<MultiStageProgressTracker>(this);
    
    // Add stages with weights
    m_multiStageTracker->addStage("Initializing", "Initializing plugin processing", 0.1);
    m_multiStageTracker->addStage("Setting up database", "Setting up database connections", 0.15);
    m_multiStageTracker->addStage("Processing log entries", "Processing log entries with plugins", 0.7);
    m_multiStageTracker->addStage("Finalizing", "Finalizing processing results", 0.05);
    
    // The multi-stage tracker will be used internally
    
    // Connect stage changes
    connect(m_multiStageTracker.get(), &MultiStageProgressTracker::stageChanged,
            this, [this](const QString& stageName, int stageIndex) {
                m_currentStage = stageName;
                emit stageChanged(stageName);
            });
}

void PluginProcessingTask::updateStageProgress(const QString& stage, int percentage)
{
    if (m_multiStageTracker) {
        m_multiStageTracker->setStageProgress(stage, percentage);
    }
    
    m_stageProgress[stage] = percentage;
}

void PluginProcessingTask::updateOverallProgress()
{
    // Calculate overall progress based on stage progress
    int totalProgress = 0;
    int stageCount = m_processingStages.size();
    
    for (const QString& stage : m_processingStages) {
        totalProgress += m_stageProgress.value(stage, 0);
    }
    
    if (stageCount > 0) {
        int overallPercentage = totalProgress / stageCount;
        setProgress(overallPercentage);
    }
}

void PluginProcessingTask::handleProcessingError(const QString& error)
{
    qWarning() << "Plugin processing error:" << error;
    m_processingResult.success = false;
    m_processingResult.errorMessage = error;
    setError(error);
}

void PluginProcessingTask::handlePluginError(const QString& pluginName, const QString& error)
{
    qWarning() << "Plugin" << pluginName << "error:" << error;
    
    // Continue processing other plugins even if one fails
    // This is a design decision - we could also fail the entire task
}

// Parallel processing event handlers
void PluginProcessingTask::onParsingProgressChanged(int percentage)
{
    // Update the "Processing log entries" stage progress
    updateStageProgress("Processing log entries", percentage);
}

void PluginProcessingTask::onParsingStatusChanged(const QString& status)
{
    // Update status without changing progress percentage
    setProgress(50, status); // Assume we're in the middle of processing stage
}

void PluginProcessingTask::onParsingCompleted(const ParseResult& result)
{
    qDebug() << "Parallel parsing completed successfully";
    m_processingResult.totalLinesProcessed = result.totalLinesProcessed;
    m_processingResult.totalRecordsInserted = result.totalRecordsInserted;
    
    updateStageProgress("Processing log entries", 100);
}

void PluginProcessingTask::onParsingCancelled()
{
    qDebug() << "Parallel parsing was cancelled";
    cancel();
}

void PluginProcessingTask::onParsingFailed(const QString& error)
{
    qWarning() << "Parallel parsing failed:" << error;
    handleProcessingError(QString("Parallel parsing failed: %1").arg(error));
}

// PluginProcessingTaskFactory implementation
std::shared_ptr<PluginProcessingTask> PluginProcessingTaskFactory::createTask(
    const QString& filePath,
    const QList<LogEntry>& logEntries,
    PluginManager* pluginManager,
    TaskPriority priority)
{
    TaskId id = generateTaskId(filePath);
    auto task = std::make_shared<PluginProcessingTask>(id, filePath, logEntries, pluginManager, priority);
    
    // Configure based on file size
    QFileInfo fileInfo(filePath);
    if (fileInfo.exists()) {
        configureTaskForFileSize(task.get(), fileInfo.size());
    }
    
    return task;
}

std::shared_ptr<PluginProcessingTask> PluginProcessingTaskFactory::createConfiguredTask(
    const QString& filePath,
    const QList<LogEntry>& logEntries,
    PluginManager* pluginManager,
    bool useParallelProcessing,
    int maxThreads,
    qint64 chunkSize,
    TaskPriority priority)
{
    TaskId id = generateTaskId(filePath);
    auto task = std::make_shared<PluginProcessingTask>(id, filePath, logEntries, pluginManager, priority);
    
    task->setUseParallelProcessing(useParallelProcessing);
    task->setMaxThreads(maxThreads);
    task->setChunkSize(chunkSize);
    
    return task;
}

std::shared_ptr<PluginProcessingTask> PluginProcessingTaskFactory::createLargeFileTask(
    const QString& filePath,
    const QList<LogEntry>& logEntries,
    PluginManager* pluginManager,
    TaskPriority priority)
{
    TaskId id = generateTaskId(filePath);
    auto task = std::make_shared<PluginProcessingTask>(id, filePath, logEntries, pluginManager, priority);
    
    // Optimize for large files
    task->setUseParallelProcessing(true);
    task->setMaxThreads(QThread::idealThreadCount());
    task->setChunkSize(2 * 1024 * 1024); // 2MB chunks
    
    return task;
}

TaskId PluginProcessingTaskFactory::generateTaskId(const QString& filePath)
{
    QString baseId = QString("plugin-processing-%1").arg(QFileInfo(filePath).baseName());
    return QString("%1-%2").arg(baseId, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void PluginProcessingTaskFactory::configureTaskForFileSize(PluginProcessingTask* task, qint64 fileSize)
{
    if (!task) {
        return;
    }
    
    // Configure based on file size
    if (fileSize < 1024 * 1024) { // < 1MB
        task->setUseParallelProcessing(false);
    } else if (fileSize < 10 * 1024 * 1024) { // < 10MB
        task->setUseParallelProcessing(true);
        task->setMaxThreads(2);
        task->setChunkSize(512 * 1024); // 512KB
    } else if (fileSize < 100 * 1024 * 1024) { // < 100MB
        task->setUseParallelProcessing(true);
        task->setMaxThreads(4);
        task->setChunkSize(1024 * 1024); // 1MB
    } else { // >= 100MB
        task->setUseParallelProcessing(true);
        task->setMaxThreads(QThread::idealThreadCount());
        task->setChunkSize(2 * 1024 * 1024); // 2MB
    }
}

#include "pluginprocessingtask.moc"