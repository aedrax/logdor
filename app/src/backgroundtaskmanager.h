#ifndef BACKGROUNDTASKMANAGER_H
#define BACKGROUNDTASKMANAGER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QMap>
#include <QTimer>
#include <QElapsedTimer>
#include <QAtomicInt>
#include <QUuid>
#include <QTime>
#include <QVariant>
#include <functional>
#include <memory>

// Forward declarations
class BackgroundTask;
class ProgressTracker;
struct ProgressInfo;

// Task identifier type
using TaskId = QString;

// Task priority levels
enum class TaskPriority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

// Task status enumeration
enum class TaskStatus {
    Queued,
    Running,
    Completed,
    Cancelled,
    Failed
};



// Task result structure
struct TaskResult {
    TaskId taskId;
    TaskStatus status;
    QString errorMessage;
    QVariant data;
    QElapsedTimer executionTime;
    
    TaskResult(const TaskId& id = QString())
        : taskId(id)
        , status(TaskStatus::Queued)
    {}
    
    bool isSuccess() const { return status == TaskStatus::Completed; }
    bool isFailure() const { return status == TaskStatus::Failed; }
};

// Background task base class
class BackgroundTask : public QObject
{
    Q_OBJECT

public:
    explicit BackgroundTask(const TaskId& id, TaskPriority priority = TaskPriority::Normal, QObject* parent = nullptr);
    virtual ~BackgroundTask();

    // Task identification
    TaskId taskId() const { return m_taskId; }
    TaskPriority priority() const { return m_priority; }
    TaskStatus status() const { return m_status; }
    
    // Task control
    virtual void execute() = 0;
    virtual void cancel();
    virtual bool canCancel() const { return true; }
    
    // Progress tracking
    void setProgressTracker(std::shared_ptr<ProgressTracker> tracker);
    std::shared_ptr<ProgressTracker> progressTracker() const { return m_progressTracker; }
    
    // Result access
    TaskResult result() const { return m_result; }
    
    // Execution state
    bool isRunning() const { return m_status == TaskStatus::Running; }
    bool isCancelled() const { return m_status == TaskStatus::Cancelled; }
    bool isCompleted() const { return m_status == TaskStatus::Completed || m_status == TaskStatus::Failed; }

signals:
    void started(const TaskId& taskId);
    void progressChanged(const TaskId& taskId, const ProgressInfo& progress);
    void statusChanged(const TaskId& taskId, const QString& status);
    void completed(const TaskId& taskId, const TaskResult& result);
    void cancelled(const TaskId& taskId);
    void failed(const TaskId& taskId, const QString& error);

protected:
    // Helper methods for derived classes
    void setStatus(TaskStatus status);
    void setProgress(int percentage, const QString& message = QString());
    void setError(const QString& error);
    void setResult(const QVariant& data);
    
    // Cancellation checking
    bool shouldCancel() const;
    void checkCancellation();

private:
    TaskId m_taskId;
    TaskPriority m_priority;
    TaskStatus m_status;
    TaskResult m_result;
    std::shared_ptr<ProgressTracker> m_progressTracker;
    mutable QRecursiveMutex m_mutex; // recursive: setResult()/cancel() re-enter setStatus()
};

// Worker thread for executing background tasks
class BackgroundWorker : public QThread
{
    Q_OBJECT

public:
    explicit BackgroundWorker(QObject* parent = nullptr);
    ~BackgroundWorker();

    // Task management
    void addTask(std::shared_ptr<BackgroundTask> task);
    void cancelTask(const TaskId& taskId);
    void cancelAllTasks();
    
    // Worker control
    void shutdown();
    bool isShuttingDown() const { return m_shuttingDown; }
    
    // Statistics
    int queuedTaskCount() const;
    int runningTaskCount() const;
    TaskId currentTaskId() const;

signals:
    void taskStarted(const TaskId& taskId);
    void taskCompleted(const TaskId& taskId, const TaskResult& result);
    void taskCancelled(const TaskId& taskId);
    void taskFailed(const TaskId& taskId, const QString& error);
    void workerIdle();

protected:
    void run() override;

private slots:
    void onTaskCompleted(const TaskId& taskId, const TaskResult& result);
    void onTaskCancelled(const TaskId& taskId);
    void onTaskFailed(const TaskId& taskId, const QString& error);

private:
    // Task queue management
    std::shared_ptr<BackgroundTask> getNextTask();
    void executeTask(std::shared_ptr<BackgroundTask> task);
    void cleanupCompletedTasks();
    
    // Priority comparison for task ordering
    static bool taskPriorityCompare(const std::shared_ptr<BackgroundTask>& a, 
                                   const std::shared_ptr<BackgroundTask>& b);

    // Task storage
    QQueue<std::shared_ptr<BackgroundTask>> m_taskQueue;
    QMap<TaskId, std::shared_ptr<BackgroundTask>> m_runningTasks;
    QMap<TaskId, std::shared_ptr<BackgroundTask>> m_allTasks;
    
    // Synchronization
    mutable QMutex m_queueMutex;
    QWaitCondition m_taskAvailable;
    
    // Worker state
    bool m_shuttingDown;
    TaskId m_currentTaskId;
    
    // Constants
    static const int CLEANUP_INTERVAL_MS;
    static const int MAX_COMPLETED_TASKS;
};

// Main background task manager
class BackgroundTaskManager : public QObject
{
    Q_OBJECT

public:
    explicit BackgroundTaskManager(QObject* parent = nullptr);
    ~BackgroundTaskManager();

    // Task submission
    TaskId submitTask(std::function<void()> taskFunction, 
                     TaskPriority priority = TaskPriority::Normal);
    TaskId submitTask(std::shared_ptr<BackgroundTask> task);
    
    // Task control
    void cancelTask(const TaskId& taskId);
    void cancelAllTasks();
    bool isTaskRunning(const TaskId& taskId) const;
    TaskStatus getTaskStatus(const TaskId& taskId) const;
    TaskResult getTaskResult(const TaskId& taskId) const;
    
    // Progress tracking
    void setProgressCallback(const TaskId& taskId, std::function<void(const ProgressInfo&)> callback);
    void setStatusCallback(const TaskId& taskId, std::function<void(const QString&)> callback);
    ProgressInfo getTaskProgress(const TaskId& taskId) const;
    
    // Manager control
    void setMaxWorkerThreads(int count);
    int maxWorkerThreads() const { return m_maxWorkerThreads; }
    void shutdown();
    
    // Statistics
    int queuedTaskCount() const;
    int runningTaskCount() const;
    int completedTaskCount() const;
    QStringList getRunningTaskIds() const;

signals:
    void taskSubmitted(const TaskId& taskId);
    void taskStarted(const TaskId& taskId);
    void taskProgressChanged(const TaskId& taskId, const ProgressInfo& progress);
    void taskStatusChanged(const TaskId& taskId, const QString& status);
    void taskCompleted(const TaskId& taskId, const TaskResult& result);
    void taskCancelled(const TaskId& taskId);
    void taskFailed(const TaskId& taskId, const QString& error);
    void allTasksCompleted();

private slots:
    void onTaskStarted(const TaskId& taskId);
    void onTaskCompleted(const TaskId& taskId, const TaskResult& result);
    void onTaskCancelled(const TaskId& taskId);
    void onTaskFailed(const TaskId& taskId, const QString& error);
    void onWorkerIdle();
    void cleanupCompletedTasks();

private:
    // Worker management
    void initializeWorkers();
    void shutdownWorkers();
    BackgroundWorker* getAvailableWorker();
    void distributeTask(std::shared_ptr<BackgroundTask> task);
    
    // Task tracking
    void registerTask(std::shared_ptr<BackgroundTask> task);
    void unregisterTask(const TaskId& taskId);
    std::shared_ptr<BackgroundTask> findTask(const TaskId& taskId) const;
    
    // Callback management
    void invokeProgressCallback(const TaskId& taskId, const ProgressInfo& progress);
    void invokeStatusCallback(const TaskId& taskId, const QString& status);
    
    // Task ID generation
    TaskId generateTaskId() const;

    // Worker threads
    QList<BackgroundWorker*> m_workers;
    int m_maxWorkerThreads;
    
    // Task tracking
    QMap<TaskId, std::shared_ptr<BackgroundTask>> m_allTasks;
    QMap<TaskId, TaskResult> m_completedTasks;
    
    // Callback storage
    QMap<TaskId, std::function<void(const ProgressInfo&)>> m_progressCallbacks;
    QMap<TaskId, std::function<void(const QString&)>> m_statusCallbacks;
    
    // Synchronization
    mutable QMutex m_tasksMutex;
    mutable QMutex m_callbacksMutex;
    mutable QMutex m_workersMutex;
    
    // Cleanup timer
    QTimer* m_cleanupTimer;
    
    // Manager state
    bool m_shuttingDown;
    
    // Constants
    static const int DEFAULT_MAX_WORKERS;
    static const int CLEANUP_INTERVAL_MS;
    static const int MAX_COMPLETED_TASKS_CACHE;
};

// Simple function-based task implementation
class FunctionTask : public BackgroundTask
{
    Q_OBJECT

public:
    explicit FunctionTask(const TaskId& id, 
                         std::function<void()> function,
                         TaskPriority priority = TaskPriority::Normal,
                         QObject* parent = nullptr);

    void execute() override;

private:
    std::function<void()> m_function;
};

#endif // BACKGROUNDTASKMANAGER_H