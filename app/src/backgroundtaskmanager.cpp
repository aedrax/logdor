#include "backgroundtaskmanager.h"
#include "progresstracker.h"
#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <algorithm>

// Constants
const int BackgroundWorker::CLEANUP_INTERVAL_MS = 5000;
const int BackgroundWorker::MAX_COMPLETED_TASKS = 100;
const int BackgroundTaskManager::DEFAULT_MAX_WORKERS = QThread::idealThreadCount();
const int BackgroundTaskManager::CLEANUP_INTERVAL_MS = 10000;
const int BackgroundTaskManager::MAX_COMPLETED_TASKS_CACHE = 200;

// BackgroundTask implementation
BackgroundTask::BackgroundTask(const TaskId& id, TaskPriority priority, QObject* parent)
    : QObject(parent)
    , m_taskId(id)
    , m_priority(priority)
    , m_status(TaskStatus::Queued)
    , m_result(id)
{
    m_result.executionTime.start();
}

BackgroundTask::~BackgroundTask()
{
}

void BackgroundTask::cancel()
{
    QMutexLocker locker(&m_mutex);
    if (m_status == TaskStatus::Running || m_status == TaskStatus::Queued) {
        setStatus(TaskStatus::Cancelled);
        emit cancelled(m_taskId);
    }
}

void BackgroundTask::setProgressTracker(std::shared_ptr<ProgressTracker> tracker)
{
    QMutexLocker locker(&m_mutex);
    m_progressTracker = tracker;
}

void BackgroundTask::setStatus(TaskStatus status)
{
    QMutexLocker locker(&m_mutex);
    if (m_status != status) {
        m_status = status;
        m_result.status = status;
        
        if (status == TaskStatus::Completed || status == TaskStatus::Failed) {
            emit completed(m_taskId, m_result);
        }
    }
}

void BackgroundTask::setProgress(int percentage, const QString& message)
{
    if (m_progressTracker) {
        ProgressInfo progress;
        progress.percentage = qBound(0, percentage, 100);
        progress.statusMessage = message;
        emit progressChanged(m_taskId, progress);
    }
    
    if (!message.isEmpty()) {
        emit statusChanged(m_taskId, message);
    }
}

void BackgroundTask::setError(const QString& error)
{
    QMutexLocker locker(&m_mutex);
    m_result.errorMessage = error;
    setStatus(TaskStatus::Failed);
    emit failed(m_taskId, error);
}

void BackgroundTask::setResult(const QVariant& data)
{
    QMutexLocker locker(&m_mutex);
    m_result.data = data;
    setStatus(TaskStatus::Completed);
}

bool BackgroundTask::shouldCancel() const
{
    QMutexLocker locker(&m_mutex);
    return m_status == TaskStatus::Cancelled;
}

void BackgroundTask::checkCancellation()
{
    if (shouldCancel()) {
        // Throw or return early - implementation depends on task type
        return;
    }
}

// BackgroundWorker implementation
BackgroundWorker::BackgroundWorker(QObject* parent)
    : QThread(parent)
    , m_shuttingDown(false)
{
}

BackgroundWorker::~BackgroundWorker()
{
    shutdown();
    wait();
}

void BackgroundWorker::addTask(std::shared_ptr<BackgroundTask> task)
{
    if (m_shuttingDown) {
        return;
    }
    
    QMutexLocker locker(&m_queueMutex);
    
    // Insert task in priority order
    auto it = std::lower_bound(m_taskQueue.begin(), m_taskQueue.end(), task, taskPriorityCompare);
    m_taskQueue.insert(it, task);
    m_allTasks[task->taskId()] = task;
    
    m_taskAvailable.wakeOne();
}

void BackgroundWorker::cancelTask(const TaskId& taskId)
{
    QMutexLocker locker(&m_queueMutex);
    
    // Cancel if in queue
    for (auto it = m_taskQueue.begin(); it != m_taskQueue.end(); ++it) {
        if ((*it)->taskId() == taskId) {
            (*it)->cancel();
            m_taskQueue.erase(it);
            break;
        }
    }
    
    // Cancel if running
    if (m_runningTasks.contains(taskId)) {
        m_runningTasks[taskId]->cancel();
    }
}

void BackgroundWorker::cancelAllTasks()
{
    QMutexLocker locker(&m_queueMutex);
    
    // Cancel queued tasks
    for (auto& task : m_taskQueue) {
        task->cancel();
    }
    m_taskQueue.clear();
    
    // Cancel running tasks
    for (auto& task : m_runningTasks) {
        task->cancel();
    }
}

void BackgroundWorker::shutdown()
{
    m_shuttingDown = true;
    cancelAllTasks();
    m_taskAvailable.wakeAll();
}

int BackgroundWorker::queuedTaskCount() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_taskQueue.size();
}

int BackgroundWorker::runningTaskCount() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_runningTasks.size();
}

TaskId BackgroundWorker::currentTaskId() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_currentTaskId;
}

void BackgroundWorker::run()
{
    while (!m_shuttingDown) {
        auto task = getNextTask();
        if (!task) {
            if (m_shuttingDown) {
                break;
            }
            emit workerIdle();
            continue;
        }
        
        executeTask(task);
        cleanupCompletedTasks();
    }
}

std::shared_ptr<BackgroundTask> BackgroundWorker::getNextTask()
{
    QMutexLocker locker(&m_queueMutex);
    
    while (m_taskQueue.isEmpty() && !m_shuttingDown) {
        m_taskAvailable.wait(&m_queueMutex, 1000); // Wait up to 1 second
    }
    
    if (m_shuttingDown || m_taskQueue.isEmpty()) {
        return nullptr;
    }
    
    auto task = m_taskQueue.dequeue();
    m_runningTasks[task->taskId()] = task;
    m_currentTaskId = task->taskId();
    
    return task;
}

void BackgroundWorker::executeTask(std::shared_ptr<BackgroundTask> task)
{
    if (!task || task->isCancelled()) {
        return;
    }
    
    // Connect task signals
    connect(task.get(), &BackgroundTask::completed, this, &BackgroundWorker::onTaskCompleted);
    connect(task.get(), &BackgroundTask::cancelled, this, &BackgroundWorker::onTaskCancelled);
    connect(task.get(), &BackgroundTask::failed, this, &BackgroundWorker::onTaskFailed);
    
    emit taskStarted(task->taskId());
    
    try {
        task->execute();
    } catch (const std::exception& e) {
        qWarning() << "Task" << task->taskId() << "threw exception:" << e.what();
        emit taskFailed(task->taskId(), QString("Exception: %1").arg(e.what()));
    } catch (...) {
        qWarning() << "Task" << task->taskId() << "threw unknown exception";
        emit taskFailed(task->taskId(), "Unknown exception occurred");
    }
}

void BackgroundWorker::onTaskCompleted(const TaskId& taskId, const TaskResult& result)
{
    {
        QMutexLocker locker(&m_queueMutex);
        m_runningTasks.remove(taskId);
        if (m_currentTaskId == taskId) {
            m_currentTaskId.clear();
        }
    }
    
    emit taskCompleted(taskId, result);
}

void BackgroundWorker::onTaskCancelled(const TaskId& taskId)
{
    {
        QMutexLocker locker(&m_queueMutex);
        m_runningTasks.remove(taskId);
        if (m_currentTaskId == taskId) {
            m_currentTaskId.clear();
        }
    }
    
    emit taskCancelled(taskId);
}

void BackgroundWorker::onTaskFailed(const TaskId& taskId, const QString& error)
{
    {
        QMutexLocker locker(&m_queueMutex);
        m_runningTasks.remove(taskId);
        if (m_currentTaskId == taskId) {
            m_currentTaskId.clear();
        }
    }
    
    emit taskFailed(taskId, error);
}

void BackgroundWorker::cleanupCompletedTasks()
{
    QMutexLocker locker(&m_queueMutex);
    
    // Remove completed tasks from all tasks map if we have too many
    if (m_allTasks.size() > MAX_COMPLETED_TASKS) {
        auto it = m_allTasks.begin();
        while (it != m_allTasks.end()) {
            if (it.value()->isCompleted() && !m_runningTasks.contains(it.key())) {
                it = m_allTasks.erase(it);
                if (m_allTasks.size() <= MAX_COMPLETED_TASKS / 2) {
                    break;
                }
            } else {
                ++it;
            }
        }
    }
}

bool BackgroundWorker::taskPriorityCompare(const std::shared_ptr<BackgroundTask>& a, 
                                          const std::shared_ptr<BackgroundTask>& b)
{
    return static_cast<int>(a->priority()) > static_cast<int>(b->priority());
}

// BackgroundTaskManager implementation
BackgroundTaskManager::BackgroundTaskManager(QObject* parent)
    : QObject(parent)
    , m_maxWorkerThreads(DEFAULT_MAX_WORKERS)
    , m_shuttingDown(false)
{
    initializeWorkers();
    
    // Setup cleanup timer
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &BackgroundTaskManager::cleanupCompletedTasks);
    m_cleanupTimer->start(CLEANUP_INTERVAL_MS);
}

BackgroundTaskManager::~BackgroundTaskManager()
{
    shutdown();
}

TaskId BackgroundTaskManager::submitTask(std::function<void()> taskFunction, TaskPriority priority)
{
    TaskId id = generateTaskId();
    auto task = std::make_shared<FunctionTask>(id, taskFunction, priority);
    return submitTask(task);
}

TaskId BackgroundTaskManager::submitTask(std::shared_ptr<BackgroundTask> task)
{
    if (m_shuttingDown) {
        return QString();
    }
    
    registerTask(task);
    distributeTask(task);
    
    emit taskSubmitted(task->taskId());
    return task->taskId();
}

void BackgroundTaskManager::cancelTask(const TaskId& taskId)
{
    QMutexLocker locker(&m_workersMutex);
    
    for (auto* worker : m_workers) {
        worker->cancelTask(taskId);
    }
    
    // Update task status
    {
        QMutexLocker taskLocker(&m_tasksMutex);
        if (m_allTasks.contains(taskId)) {
            m_allTasks[taskId]->cancel();
        }
    }
}

void BackgroundTaskManager::cancelAllTasks()
{
    QMutexLocker locker(&m_workersMutex);
    
    for (auto* worker : m_workers) {
        worker->cancelAllTasks();
    }
    
    // Update all task statuses
    {
        QMutexLocker taskLocker(&m_tasksMutex);
        for (auto& task : m_allTasks) {
            if (!task->isCompleted()) {
                task->cancel();
            }
        }
    }
}

bool BackgroundTaskManager::isTaskRunning(const TaskId& taskId) const
{
    QMutexLocker locker(&m_tasksMutex);
    auto task = findTask(taskId);
    return task && task->isRunning();
}

TaskStatus BackgroundTaskManager::getTaskStatus(const TaskId& taskId) const
{
    QMutexLocker locker(&m_tasksMutex);
    auto task = findTask(taskId);
    return task ? task->status() : TaskStatus::Failed;
}

TaskResult BackgroundTaskManager::getTaskResult(const TaskId& taskId) const
{
    QMutexLocker locker(&m_tasksMutex);
    
    // Check completed tasks cache first
    if (m_completedTasks.contains(taskId)) {
        return m_completedTasks[taskId];
    }
    
    // Check active tasks
    auto task = findTask(taskId);
    return task ? task->result() : TaskResult(taskId);
}

void BackgroundTaskManager::setProgressCallback(const TaskId& taskId, std::function<void(const ProgressInfo&)> callback)
{
    QMutexLocker locker(&m_callbacksMutex);
    m_progressCallbacks[taskId] = callback;
}

void BackgroundTaskManager::setStatusCallback(const TaskId& taskId, std::function<void(const QString&)> callback)
{
    QMutexLocker locker(&m_callbacksMutex);
    m_statusCallbacks[taskId] = callback;
}

ProgressInfo BackgroundTaskManager::getTaskProgress(const TaskId& taskId) const
{
    QMutexLocker locker(&m_tasksMutex);
    auto task = findTask(taskId);
    
    if (task && task->progressTracker()) {
        // Return progress from tracker if available
        // This would need to be implemented in ProgressTracker
        return ProgressInfo();
    }
    
    return ProgressInfo();
}

void BackgroundTaskManager::setMaxWorkerThreads(int count)
{
    if (count <= 0 || count == m_maxWorkerThreads) {
        return;
    }
    
    QMutexLocker locker(&m_workersMutex);
    
    if (count > m_maxWorkerThreads) {
        // Add more workers
        for (int i = m_maxWorkerThreads; i < count; ++i) {
            auto* worker = new BackgroundWorker(this);
            
            // Connect worker signals
            connect(worker, &BackgroundWorker::taskStarted, this, &BackgroundTaskManager::onTaskStarted);
            connect(worker, &BackgroundWorker::taskCompleted, this, &BackgroundTaskManager::onTaskCompleted);
            connect(worker, &BackgroundWorker::taskCancelled, this, &BackgroundTaskManager::onTaskCancelled);
            connect(worker, &BackgroundWorker::taskFailed, this, &BackgroundTaskManager::onTaskFailed);
            connect(worker, &BackgroundWorker::workerIdle, this, &BackgroundTaskManager::onWorkerIdle);
            
            worker->start();
            m_workers.append(worker);
        }
    } else {
        // Remove excess workers
        while (m_workers.size() > count) {
            auto* worker = m_workers.takeLast();
            worker->shutdown();
            worker->wait();
            worker->deleteLater();
        }
    }
    
    m_maxWorkerThreads = count;
}

void BackgroundTaskManager::shutdown()
{
    if (m_shuttingDown) {
        return;
    }
    
    m_shuttingDown = true;
    m_cleanupTimer->stop();
    
    cancelAllTasks();
    shutdownWorkers();
}

int BackgroundTaskManager::queuedTaskCount() const
{
    QMutexLocker locker(&m_workersMutex);
    int total = 0;
    for (const auto* worker : m_workers) {
        total += worker->queuedTaskCount();
    }
    return total;
}

int BackgroundTaskManager::runningTaskCount() const
{
    QMutexLocker locker(&m_workersMutex);
    int total = 0;
    for (const auto* worker : m_workers) {
        total += worker->runningTaskCount();
    }
    return total;
}

int BackgroundTaskManager::completedTaskCount() const
{
    QMutexLocker locker(&m_tasksMutex);
    return m_completedTasks.size();
}

QStringList BackgroundTaskManager::getRunningTaskIds() const
{
    QMutexLocker locker(&m_workersMutex);
    QStringList ids;
    
    for (const auto* worker : m_workers) {
        TaskId currentId = worker->currentTaskId();
        if (!currentId.isEmpty()) {
            ids.append(currentId);
        }
    }
    
    return ids;
}

void BackgroundTaskManager::onTaskStarted(const TaskId& taskId)
{
    emit taskStarted(taskId);
}

void BackgroundTaskManager::onTaskCompleted(const TaskId& taskId, const TaskResult& result)
{
    {
        QMutexLocker locker(&m_tasksMutex);
        m_completedTasks[taskId] = result;
        m_allTasks.remove(taskId);
    }
    
    emit taskCompleted(taskId, result);
    
    // Check if all tasks are completed
    if (queuedTaskCount() == 0 && runningTaskCount() == 0) {
        emit allTasksCompleted();
    }
}

void BackgroundTaskManager::onTaskCancelled(const TaskId& taskId)
{
    {
        QMutexLocker locker(&m_tasksMutex);
        m_allTasks.remove(taskId);
    }
    
    emit taskCancelled(taskId);
}

void BackgroundTaskManager::onTaskFailed(const TaskId& taskId, const QString& error)
{
    TaskResult result(taskId);
    result.status = TaskStatus::Failed;
    result.errorMessage = error;
    
    {
        QMutexLocker locker(&m_tasksMutex);
        m_completedTasks[taskId] = result;
        m_allTasks.remove(taskId);
    }
    
    emit taskFailed(taskId, error);
}

void BackgroundTaskManager::onWorkerIdle()
{
    // Could implement load balancing here if needed
}

void BackgroundTaskManager::cleanupCompletedTasks()
{
    QMutexLocker locker(&m_tasksMutex);
    
    if (m_completedTasks.size() > MAX_COMPLETED_TASKS_CACHE) {
        // Remove oldest completed tasks
        auto it = m_completedTasks.begin();
        int toRemove = m_completedTasks.size() - (MAX_COMPLETED_TASKS_CACHE / 2);
        
        for (int i = 0; i < toRemove && it != m_completedTasks.end(); ++i) {
            it = m_completedTasks.erase(it);
        }
    }
    
    // Clean up callbacks for completed tasks
    {
        QMutexLocker callbackLocker(&m_callbacksMutex);
        
        auto progressIt = m_progressCallbacks.begin();
        while (progressIt != m_progressCallbacks.end()) {
            if (m_completedTasks.contains(progressIt.key()) || !m_allTasks.contains(progressIt.key())) {
                progressIt = m_progressCallbacks.erase(progressIt);
            } else {
                ++progressIt;
            }
        }
        
        auto statusIt = m_statusCallbacks.begin();
        while (statusIt != m_statusCallbacks.end()) {
            if (m_completedTasks.contains(statusIt.key()) || !m_allTasks.contains(statusIt.key())) {
                statusIt = m_statusCallbacks.erase(statusIt);
            } else {
                ++statusIt;
            }
        }
    }
}

void BackgroundTaskManager::initializeWorkers()
{
    QMutexLocker locker(&m_workersMutex);
    
    for (int i = 0; i < m_maxWorkerThreads; ++i) {
        auto* worker = new BackgroundWorker(this);
        
        // Connect worker signals
        connect(worker, &BackgroundWorker::taskStarted, this, &BackgroundTaskManager::onTaskStarted);
        connect(worker, &BackgroundWorker::taskCompleted, this, &BackgroundTaskManager::onTaskCompleted);
        connect(worker, &BackgroundWorker::taskCancelled, this, &BackgroundTaskManager::onTaskCancelled);
        connect(worker, &BackgroundWorker::taskFailed, this, &BackgroundTaskManager::onTaskFailed);
        connect(worker, &BackgroundWorker::workerIdle, this, &BackgroundTaskManager::onWorkerIdle);
        
        worker->start();
        m_workers.append(worker);
    }
}

void BackgroundTaskManager::shutdownWorkers()
{
    QMutexLocker locker(&m_workersMutex);
    
    for (auto* worker : m_workers) {
        worker->shutdown();
    }
    
    for (auto* worker : m_workers) {
        worker->wait();
        worker->deleteLater();
    }
    
    m_workers.clear();
}

BackgroundWorker* BackgroundTaskManager::getAvailableWorker()
{
    QMutexLocker locker(&m_workersMutex);
    
    // Find worker with least queued tasks
    BackgroundWorker* bestWorker = nullptr;
    int minQueueSize = INT_MAX;
    
    for (auto* worker : m_workers) {
        int queueSize = worker->queuedTaskCount();
        if (queueSize < minQueueSize) {
            minQueueSize = queueSize;
            bestWorker = worker;
        }
    }
    
    return bestWorker;
}

void BackgroundTaskManager::distributeTask(std::shared_ptr<BackgroundTask> task)
{
    auto* worker = getAvailableWorker();
    if (worker) {
        // Connect task progress signals to manager
        connect(task.get(), &BackgroundTask::progressChanged, 
                this, [this](const TaskId& taskId, const ProgressInfo& progress) {
                    invokeProgressCallback(taskId, progress);
                    emit taskProgressChanged(taskId, progress);
                });
        
        connect(task.get(), &BackgroundTask::statusChanged,
                this, [this](const TaskId& taskId, const QString& status) {
                    invokeStatusCallback(taskId, status);
                    emit taskStatusChanged(taskId, status);
                });
        
        worker->addTask(task);
    }
}

void BackgroundTaskManager::registerTask(std::shared_ptr<BackgroundTask> task)
{
    QMutexLocker locker(&m_tasksMutex);
    m_allTasks[task->taskId()] = task;
}

void BackgroundTaskManager::unregisterTask(const TaskId& taskId)
{
    QMutexLocker locker(&m_tasksMutex);
    m_allTasks.remove(taskId);
}

std::shared_ptr<BackgroundTask> BackgroundTaskManager::findTask(const TaskId& taskId) const
{
    auto it = m_allTasks.find(taskId);
    return it != m_allTasks.end() ? it.value() : nullptr;
}

void BackgroundTaskManager::invokeProgressCallback(const TaskId& taskId, const ProgressInfo& progress)
{
    QMutexLocker locker(&m_callbacksMutex);
    auto it = m_progressCallbacks.find(taskId);
    if (it != m_progressCallbacks.end()) {
        it.value()(progress);
    }
}

void BackgroundTaskManager::invokeStatusCallback(const TaskId& taskId, const QString& status)
{
    QMutexLocker locker(&m_callbacksMutex);
    auto it = m_statusCallbacks.find(taskId);
    if (it != m_statusCallbacks.end()) {
        it.value()(status);
    }
}

TaskId BackgroundTaskManager::generateTaskId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// FunctionTask implementation
FunctionTask::FunctionTask(const TaskId& id, std::function<void()> function, TaskPriority priority, QObject* parent)
    : BackgroundTask(id, priority, parent)
    , m_function(function)
{
}

void FunctionTask::execute()
{
    if (shouldCancel()) {
        return;
    }
    
    setStatus(TaskStatus::Running);
    emit started(taskId());
    
    try {
        if (m_function) {
            m_function();
        }
        setResult(QVariant());
    } catch (const std::exception& e) {
        setError(QString("Function execution failed: %1").arg(e.what()));
    } catch (...) {
        setError("Function execution failed with unknown exception");
    }
}

#include "backgroundtaskmanager.moc"