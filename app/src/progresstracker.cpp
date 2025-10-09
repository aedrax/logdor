#include "progresstracker.h"
#include "backgroundtaskmanager.h"
#include <QDebug>
#include <QCoreApplication>
#include <algorithm>

// Constants
const int ProgressTracker::DEFAULT_UPDATE_INTERVAL_MS = 100;
const int ProgressTracker::METRICS_HISTORY_SIZE = 50;
const int ProgressTracker::MIN_SAMPLES_FOR_ESTIMATION = 3;

// ProgressTracker implementation
ProgressTracker::ProgressTracker(QObject* parent)
    : QObject(parent)
    , m_totalItems(0)
    , m_updateInterval(DEFAULT_UPDATE_INTERVAL_MS)
    , m_processedItems(0)
    , m_processedBytes(0)
    , m_completed(false)
    , m_cancelled(false)
    , m_failed(false)
{
    initialize();
}

ProgressTracker::ProgressTracker(qint64 totalItems, const QString& description, QObject* parent)
    : QObject(parent)
    , m_totalItems(totalItems)
    , m_description(description)
    , m_updateInterval(DEFAULT_UPDATE_INTERVAL_MS)
    , m_processedItems(0)
    , m_processedBytes(0)
    , m_completed(false)
    , m_cancelled(false)
    , m_failed(false)
{
    initialize();
}

ProgressTracker::~ProgressTracker()
{
    if (m_metricsTimer) {
        m_metricsTimer->stop();
    }
}

void ProgressTracker::initialize()
{
    m_elapsedTimer.start();
    
    // Setup metrics update timer
    m_metricsTimer = new QTimer(this);
    connect(m_metricsTimer, &QTimer::timeout, this, &ProgressTracker::updateMetrics);
    m_metricsTimer->start(m_updateInterval);
}

void ProgressTracker::setTotalItems(qint64 total)
{
    QMutexLocker locker(&m_mutex);
    m_totalItems = total;
    calculateMetrics();
    emitProgressUpdate();
}

void ProgressTracker::setDescription(const QString& description)
{
    QMutexLocker locker(&m_mutex);
    m_description = description;
}

void ProgressTracker::setUpdateInterval(int milliseconds)
{
    QMutexLocker locker(&m_mutex);
    m_updateInterval = milliseconds;
    if (m_metricsTimer) {
        m_metricsTimer->setInterval(milliseconds);
    }
}

void ProgressTracker::setProgress(qint64 processedItems, const QString& statusMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_completed || m_cancelled || m_failed) {
        return;
    }
    
    m_processedItems.storeRelaxed(static_cast<int>(qMin(processedItems, m_totalItems)));
    
    if (!statusMessage.isEmpty()) {
        m_currentStatus = statusMessage;
    }
    
    calculateMetrics();
    emitProgressUpdate();
    
    // Auto-complete if we've reached the total
    if (processedItems >= m_totalItems && m_totalItems > 0) {
        complete();
    }
}

void ProgressTracker::incrementProgress(qint64 itemsDelta, const QString& statusMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_completed || m_cancelled || m_failed) {
        return;
    }
    
    qint64 newProcessed = m_processedItems.loadRelaxed() + itemsDelta;
    m_processedItems.storeRelaxed(static_cast<int>(qMin(newProcessed, m_totalItems)));
    
    if (!statusMessage.isEmpty()) {
        m_currentStatus = statusMessage;
    }
    
    calculateMetrics();
    emitProgressUpdate();
    
    // Auto-complete if we've reached the total
    if (newProcessed >= m_totalItems && m_totalItems > 0) {
        complete();
    }
}

void ProgressTracker::setProgressPercentage(int percentage, const QString& statusMessage)
{
    if (m_totalItems <= 0) {
        return;
    }
    
    qint64 processedItems = (static_cast<qint64>(qBound(0, percentage, 100)) * m_totalItems) / 100;
    setProgress(processedItems, statusMessage);
}

void ProgressTracker::addProcessedBytes(qint64 bytes)
{
    QMutexLocker locker(&m_mutex);
    m_processedBytes += bytes;
    
    // Record byte processing for metrics
    qint64 timestamp = m_elapsedTimer.elapsed();
    m_recentBytes.enqueue(qMakePair(timestamp, m_processedBytes));
    
    // Keep only recent samples
    while (m_recentBytes.size() > METRICS_HISTORY_SIZE) {
        m_recentBytes.dequeue();
    }
}

void ProgressTracker::setStatusMessage(const QString& message)
{
    QMutexLocker locker(&m_mutex);
    m_currentStatus = message;
    
    if (m_statusCallback) {
        m_statusCallback(message);
    }
    
    emit statusChanged(message);
}

void ProgressTracker::complete(const QString& completionMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_completed) {
        return;
    }
    
    m_completed = true;
    m_processedItems.storeRelaxed(static_cast<int>(m_totalItems));
    
    if (!completionMessage.isEmpty()) {
        m_currentStatus = completionMessage;
    } else {
        m_currentStatus = "Completed";
    }
    
    if (m_metricsTimer) {
        m_metricsTimer->stop();
    }
    
    calculateMetrics();
    emitProgressUpdate();
    
    if (m_completionCallback) {
        m_completionCallback();
    }
    
    emit completed();
}

void ProgressTracker::cancel(const QString& cancellationMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_completed || m_cancelled) {
        return;
    }
    
    m_cancelled = true;
    
    if (!cancellationMessage.isEmpty()) {
        m_currentStatus = cancellationMessage;
    } else {
        m_currentStatus = "Cancelled";
    }
    
    if (m_metricsTimer) {
        m_metricsTimer->stop();
    }
    
    emit cancelled();
}

void ProgressTracker::fail(const QString& errorMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_completed || m_failed) {
        return;
    }
    
    m_failed = true;
    m_currentStatus = errorMessage.isEmpty() ? "Failed" : errorMessage;
    
    if (m_metricsTimer) {
        m_metricsTimer->stop();
    }
    
    emit failed(errorMessage);
}

ProgressInfo ProgressTracker::currentProgress() const
{
    QMutexLocker locker(&m_mutex);
    
    ProgressInfo info;
    info.percentage = percentage();
    info.statusMessage = m_currentStatus;
    info.processedItems = m_processedItems.loadRelaxed();
    info.totalItems = m_totalItems;
    info.estimatedRemaining = estimatedTimeRemaining();
    info.itemsPerSecond = m_metrics.itemsPerSecond;
    
    return info;
}

ProgressMetrics ProgressTracker::currentMetrics() const
{
    QMutexLocker locker(&m_mutex);
    return m_metrics;
}

int ProgressTracker::percentage() const
{
    if (m_totalItems <= 0) {
        return m_completed ? 100 : 0;
    }
    
    return static_cast<int>((m_processedItems.loadRelaxed() * 100) / m_totalItems);
}

QString ProgressTracker::statusMessage() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentStatus;
}

double ProgressTracker::itemsPerSecond() const
{
    QMutexLocker locker(&m_mutex);
    return m_metrics.itemsPerSecond;
}

double ProgressTracker::bytesPerSecond() const
{
    QMutexLocker locker(&m_mutex);
    return m_metrics.bytesPerSecond;
}

QTime ProgressTracker::estimatedTimeRemaining() const
{
    QMutexLocker locker(&m_mutex);
    return m_metrics.estimatedTimeRemaining;
}

QTime ProgressTracker::elapsedTime() const
{
    qint64 elapsed = m_elapsedTimer.elapsed();
    return QTime(0, 0).addMSecs(static_cast<int>(elapsed));
}

void ProgressTracker::setProgressCallback(std::function<void(const ProgressInfo&)> callback)
{
    QMutexLocker locker(&m_mutex);
    m_progressCallback = callback;
}

void ProgressTracker::setStatusCallback(std::function<void(const QString&)> callback)
{
    QMutexLocker locker(&m_mutex);
    m_statusCallback = callback;
}

void ProgressTracker::setCompletionCallback(std::function<void()> callback)
{
    QMutexLocker locker(&m_mutex);
    m_completionCallback = callback;
}

void ProgressTracker::updateMetrics()
{
    QMutexLocker locker(&m_mutex);
    calculateMetrics();
    emitProgressUpdate();
}

void ProgressTracker::calculateMetrics()
{
    qint64 currentTime = m_elapsedTimer.elapsed();
    qint64 currentItems = m_processedItems.loadRelaxed();
    
    // Record current progress for metrics
    m_recentProgress.enqueue(qMakePair(currentTime, currentItems));
    
    // Keep only recent samples
    while (m_recentProgress.size() > METRICS_HISTORY_SIZE) {
        m_recentProgress.dequeue();
    }
    
    // Calculate items per second
    if (m_recentProgress.size() >= MIN_SAMPLES_FOR_ESTIMATION) {
        auto oldest = m_recentProgress.first();
        auto newest = m_recentProgress.last();
        
        qint64 timeDiff = newest.first - oldest.first;
        qint64 itemsDiff = newest.second - oldest.second;
        
        if (timeDiff > 0) {
            m_metrics.itemsPerSecond = (itemsDiff * 1000.0) / timeDiff;
        }
    }
    
    // Calculate bytes per second
    if (m_recentBytes.size() >= MIN_SAMPLES_FOR_ESTIMATION) {
        auto oldest = m_recentBytes.first();
        auto newest = m_recentBytes.last();
        
        qint64 timeDiff = newest.first - oldest.first;
        qint64 bytesDiff = newest.second - oldest.second;
        
        if (timeDiff > 0) {
            m_metrics.bytesPerSecond = (bytesDiff * 1000.0) / timeDiff;
        }
    }
    
    // Calculate estimated time remaining
    if (m_metrics.itemsPerSecond > 0 && m_totalItems > 0) {
        qint64 remainingItems = m_totalItems - currentItems;
        if (remainingItems > 0) {
            double remainingSeconds = remainingItems / m_metrics.itemsPerSecond;
            m_metrics.estimatedTimeRemaining = QTime(0, 0).addSecs(static_cast<int>(remainingSeconds));
        } else {
            m_metrics.estimatedTimeRemaining = QTime(0, 0);
        }
    }
    
    // Update other metrics
    m_metrics.elapsedTime = QTime(0, 0).addMSecs(static_cast<int>(currentTime));
    m_metrics.totalItemsProcessed = currentItems;
    m_metrics.totalBytesProcessed = m_processedBytes;
}

void ProgressTracker::emitProgressUpdate()
{
    ProgressInfo info = currentProgress();
    
    if (m_progressCallback) {
        m_progressCallback(info);
    }
    
    emit progressChanged(info);
}

void ProgressTracker::invokeCallbacks()
{
    ProgressInfo info = currentProgress();
    
    if (m_progressCallback) {
        m_progressCallback(info);
    }
    
    if (m_statusCallback && !m_currentStatus.isEmpty()) {
        m_statusCallback(m_currentStatus);
    }
}

// MultiStageProgressTracker implementation
MultiStageProgressTracker::MultiStageProgressTracker(QObject* parent)
    : QObject(parent)
    , m_currentStageIndex(-1)
    , m_cancelled(false)
    , m_failed(false)
{
    m_elapsedTimer.start();
}

MultiStageProgressTracker::~MultiStageProgressTracker()
{
}

void MultiStageProgressTracker::addStage(const QString& name, const QString& description, double weight)
{
    addStage(ProgressStage(name, description, weight));
}

void MultiStageProgressTracker::addStage(const ProgressStage& stage)
{
    QMutexLocker locker(&m_mutex);
    
    if (stage.isValid()) {
        m_stages.append(stage);
        validateStageWeights();
        
        // Set first stage as current if none is set
        if (m_currentStageIndex < 0 && !m_stages.isEmpty()) {
            m_currentStageIndex = 0;
        }
    }
}

void MultiStageProgressTracker::setCurrentStage(const QString& stageName)
{
    QMutexLocker locker(&m_mutex);
    
    for (int i = 0; i < m_stages.size(); ++i) {
        if (m_stages[i].name == stageName) {
            setCurrentStage(i);
            return;
        }
    }
    
    qWarning() << "Stage not found:" << stageName;
}

void MultiStageProgressTracker::setCurrentStage(int stageIndex)
{
    QMutexLocker locker(&m_mutex);
    
    if (stageIndex >= 0 && stageIndex < m_stages.size()) {
        m_currentStageIndex = stageIndex;
        
        QString stageName = m_stages[stageIndex].name;
        
        if (m_stageChangedCallback) {
            m_stageChangedCallback(stageName, stageIndex);
        }
        
        emit stageChanged(stageName, stageIndex);
        calculateOverallProgress();
        emitProgressUpdate();
    }
}

void MultiStageProgressTracker::setStageProgress(int percentage, const QString& statusMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_currentStageIndex >= 0 && m_currentStageIndex < m_stages.size()) {
        m_stages[m_currentStageIndex].percentage = qBound(0, percentage, 100);
        
        if (!statusMessage.isEmpty()) {
            m_currentStatusMessage = statusMessage;
        }
        
        calculateOverallProgress();
        emitProgressUpdate();
        
        // Auto-complete stage if at 100%
        if (percentage >= 100) {
            completeCurrentStage();
        }
    }
}

void MultiStageProgressTracker::setStageProgress(const QString& stageName, int percentage, const QString& statusMessage)
{
    QMutexLocker locker(&m_mutex);
    
    for (int i = 0; i < m_stages.size(); ++i) {
        if (m_stages[i].name == stageName) {
            m_stages[i].percentage = qBound(0, percentage, 100);
            
            if (i == m_currentStageIndex && !statusMessage.isEmpty()) {
                m_currentStatusMessage = statusMessage;
            }
            
            calculateOverallProgress();
            emitProgressUpdate();
            
            // Auto-complete stage if at 100%
            if (percentage >= 100) {
                completeStage(stageName);
            }
            return;
        }
    }
    
    qWarning() << "Stage not found:" << stageName;
}

void MultiStageProgressTracker::completeCurrentStage(const QString& completionMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_currentStageIndex >= 0 && m_currentStageIndex < m_stages.size()) {
        m_stages[m_currentStageIndex].percentage = 100;
        m_stages[m_currentStageIndex].completed = true;
        
        if (!completionMessage.isEmpty()) {
            m_currentStatusMessage = completionMessage;
        }
        
        QString stageName = m_stages[m_currentStageIndex].name;
        
        emit stageCompleted(stageName, m_currentStageIndex);
        
        // Move to next stage if available
        if (m_currentStageIndex + 1 < m_stages.size()) {
            setCurrentStage(m_currentStageIndex + 1);
        } else {
            // All stages completed
            complete();
        }
    }
}

void MultiStageProgressTracker::completeStage(const QString& stageName, const QString& completionMessage)
{
    QMutexLocker locker(&m_mutex);
    
    for (int i = 0; i < m_stages.size(); ++i) {
        if (m_stages[i].name == stageName) {
            m_stages[i].percentage = 100;
            m_stages[i].completed = true;
            
            if (i == m_currentStageIndex && !completionMessage.isEmpty()) {
                m_currentStatusMessage = completionMessage;
            }
            
            emit stageCompleted(stageName, i);
            
            // If this was the current stage, move to next
            if (i == m_currentStageIndex) {
                if (i + 1 < m_stages.size()) {
                    setCurrentStage(i + 1);
                } else {
                    complete();
                }
            }
            
            calculateOverallProgress();
            emitProgressUpdate();
            return;
        }
    }
    
    qWarning() << "Stage not found:" << stageName;
}

ProgressInfo MultiStageProgressTracker::currentProgress() const
{
    QMutexLocker locker(&m_mutex);
    
    ProgressInfo info;
    info.percentage = overallPercentage();
    info.statusMessage = m_currentStatusMessage;
    info.estimatedRemaining = QTime(0, 0); // Could be calculated based on stage progress
    
    return info;
}

int MultiStageProgressTracker::overallPercentage() const
{
    if (m_stages.isEmpty()) {
        return 0;
    }
    
    double totalWeight = 0.0;
    double completedWeight = 0.0;
    
    for (const auto& stage : m_stages) {
        totalWeight += stage.weight;
        completedWeight += stage.weight * (stage.percentage / 100.0);
    }
    
    if (totalWeight <= 0.0) {
        return 0;
    }
    
    return static_cast<int>((completedWeight / totalWeight) * 100.0);
}

QString MultiStageProgressTracker::currentStageDescription() const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_currentStageIndex >= 0 && m_currentStageIndex < m_stages.size()) {
        return m_stages[m_currentStageIndex].description;
    }
    
    return QString();
}

QString MultiStageProgressTracker::currentStatusMessage() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentStatusMessage;
}

QList<ProgressStage> MultiStageProgressTracker::stages() const
{
    QMutexLocker locker(&m_mutex);
    return m_stages;
}

ProgressStage MultiStageProgressTracker::currentStage() const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_currentStageIndex >= 0 && m_currentStageIndex < m_stages.size()) {
        return m_stages[m_currentStageIndex];
    }
    
    return ProgressStage();
}

int MultiStageProgressTracker::stageCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_stages.size();
}

bool MultiStageProgressTracker::isCompleted() const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_stages.isEmpty()) {
        return false;
    }
    
    for (const auto& stage : m_stages) {
        if (!stage.completed) {
            return false;
        }
    }
    
    return true;
}

void MultiStageProgressTracker::complete(const QString& completionMessage)
{
    QMutexLocker locker(&m_mutex);
    
    // Mark all stages as completed
    for (auto& stage : m_stages) {
        stage.percentage = 100;
        stage.completed = true;
    }
    
    if (!completionMessage.isEmpty()) {
        m_currentStatusMessage = completionMessage;
    } else {
        m_currentStatusMessage = "All stages completed";
    }
    
    calculateOverallProgress();
    emitProgressUpdate();
    
    if (m_completionCallback) {
        m_completionCallback();
    }
    
    emit completed();
}

void MultiStageProgressTracker::cancel(const QString& cancellationMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_cancelled) {
        return;
    }
    
    m_cancelled = true;
    
    if (!cancellationMessage.isEmpty()) {
        m_currentStatusMessage = cancellationMessage;
    } else {
        m_currentStatusMessage = "Operation cancelled";
    }
    
    emit cancelled();
}

void MultiStageProgressTracker::fail(const QString& errorMessage)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_failed) {
        return;
    }
    
    m_failed = true;
    m_errorMessage = errorMessage;
    m_currentStatusMessage = errorMessage.isEmpty() ? "Operation failed" : errorMessage;
    
    emit failed(errorMessage);
}

void MultiStageProgressTracker::reset()
{
    QMutexLocker locker(&m_mutex);
    
    for (auto& stage : m_stages) {
        stage.percentage = 0;
        stage.completed = false;
    }
    
    m_currentStageIndex = m_stages.isEmpty() ? -1 : 0;
    m_currentStatusMessage.clear();
    m_cancelled = false;
    m_failed = false;
    m_errorMessage.clear();
    
    m_elapsedTimer.restart();
    
    calculateOverallProgress();
    emitProgressUpdate();
}

void MultiStageProgressTracker::setProgressCallback(std::function<void(const ProgressInfo&)> callback)
{
    QMutexLocker locker(&m_mutex);
    m_progressCallback = callback;
}

void MultiStageProgressTracker::setStatusCallback(std::function<void(const QString&)> callback)
{
    QMutexLocker locker(&m_mutex);
    m_statusCallback = callback;
}

void MultiStageProgressTracker::setStageChangedCallback(std::function<void(const QString&, int)> callback)
{
    QMutexLocker locker(&m_mutex);
    m_stageChangedCallback = callback;
}

void MultiStageProgressTracker::setCompletionCallback(std::function<void()> callback)
{
    QMutexLocker locker(&m_mutex);
    m_completionCallback = callback;
}

void MultiStageProgressTracker::calculateOverallProgress()
{
    // Progress calculation is done in overallPercentage()
}

void MultiStageProgressTracker::emitProgressUpdate()
{
    ProgressInfo info = currentProgress();
    
    if (m_progressCallback) {
        m_progressCallback(info);
    }
    
    emit progressChanged(info);
    
    if (m_statusCallback && !m_currentStatusMessage.isEmpty()) {
        m_statusCallback(m_currentStatusMessage);
        emit statusChanged(m_currentStatusMessage);
    }
}

void MultiStageProgressTracker::invokeCallbacks()
{
    emitProgressUpdate();
}

void MultiStageProgressTracker::validateStageWeights()
{
    // Ensure all weights are positive
    for (auto& stage : m_stages) {
        if (stage.weight <= 0.0) {
            stage.weight = 1.0;
        }
    }
}

// ProgressAggregator implementation
ProgressAggregator::ProgressAggregator(QObject* parent)
    : QObject(parent)
{
}

ProgressAggregator::~ProgressAggregator()
{
    clearTrackers();
}

void ProgressAggregator::addTracker(const QString& name, ProgressTracker* tracker, double weight)
{
    if (!tracker || name.isEmpty() || weight <= 0.0) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    TrackerInfo info(tracker, weight, false);
    m_trackers[name] = info;
    
    connectTracker(name, info);
    updateAggregatedProgress();
}

void ProgressAggregator::addTracker(const QString& name, MultiStageProgressTracker* tracker, double weight)
{
    if (!tracker || name.isEmpty() || weight <= 0.0) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    TrackerInfo info(tracker, weight, true);
    m_trackers[name] = info;
    
    connectTracker(name, info);
    updateAggregatedProgress();
}

void ProgressAggregator::removeTracker(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    auto it = m_trackers.find(name);
    if (it != m_trackers.end()) {
        disconnectTracker(name, it.value());
        m_trackers.erase(it);
        updateAggregatedProgress();
    }
}

void ProgressAggregator::clearTrackers()
{
    QMutexLocker locker(&m_mutex);
    
    for (auto it = m_trackers.begin(); it != m_trackers.end(); ++it) {
        disconnectTracker(it.key(), it.value());
    }
    
    m_trackers.clear();
    updateAggregatedProgress();
}

void ProgressAggregator::setTrackerWeight(const QString& name, double weight)
{
    if (weight <= 0.0) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    auto it = m_trackers.find(name);
    if (it != m_trackers.end()) {
        it.value().weight = weight;
        updateAggregatedProgress();
    }
}

double ProgressAggregator::getTrackerWeight(const QString& name) const
{
    QMutexLocker locker(&m_mutex);
    
    auto it = m_trackers.find(name);
    return it != m_trackers.end() ? it.value().weight : 0.0;
}

ProgressInfo ProgressAggregator::aggregatedProgress() const
{
    QMutexLocker locker(&m_mutex);
    return m_aggregatedProgress;
}

int ProgressAggregator::overallPercentage() const
{
    QMutexLocker locker(&m_mutex);
    return m_aggregatedProgress.percentage;
}

QString ProgressAggregator::currentStatus() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentStatus;
}

QStringList ProgressAggregator::activeTrackerNames() const
{
    QMutexLocker locker(&m_mutex);
    
    QStringList names;
    for (auto it = m_trackers.begin(); it != m_trackers.end(); ++it) {
        if (!it.value().completed && !it.value().failed) {
            names.append(it.key());
        }
    }
    
    return names;
}

bool ProgressAggregator::isCompleted() const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_trackers.isEmpty()) {
        return false;
    }
    
    for (const auto& info : m_trackers) {
        if (!info.completed) {
            return false;
        }
    }
    
    return true;
}

bool ProgressAggregator::hasFailures() const
{
    QMutexLocker locker(&m_mutex);
    
    for (const auto& info : m_trackers) {
        if (info.failed) {
            return true;
        }
    }
    
    return false;
}

QStringList ProgressAggregator::failedTrackerNames() const
{
    QMutexLocker locker(&m_mutex);
    
    QStringList names;
    for (auto it = m_trackers.begin(); it != m_trackers.end(); ++it) {
        if (it.value().failed) {
            names.append(it.key());
        }
    }
    
    return names;
}

void ProgressAggregator::setProgressCallback(std::function<void(const ProgressInfo&)> callback)
{
    QMutexLocker locker(&m_mutex);
    m_progressCallback = callback;
}

void ProgressAggregator::setStatusCallback(std::function<void(const QString&)> callback)
{
    QMutexLocker locker(&m_mutex);
    m_statusCallback = callback;
}

void ProgressAggregator::setCompletionCallback(std::function<void()> callback)
{
    QMutexLocker locker(&m_mutex);
    m_completionCallback = callback;
}

void ProgressAggregator::onTrackerProgressChanged(const ProgressInfo& progress)
{
    updateAggregatedProgress();
}

void ProgressAggregator::onTrackerStatusChanged(const QString& status)
{
    QMutexLocker locker(&m_mutex);
    
    // Find which tracker sent this status
    QObject* sender = QObject::sender();
    for (auto it = m_trackers.begin(); it != m_trackers.end(); ++it) {
        if (it.value().tracker == sender) {
            it.value().lastStatus = status;
            m_currentStatus = QString("%1: %2").arg(it.key(), status);
            
            if (m_statusCallback) {
                m_statusCallback(m_currentStatus);
            }
            
            emit statusChanged(m_currentStatus);
            break;
        }
    }
}

void ProgressAggregator::onTrackerCompleted()
{
    QObject* sender = QObject::sender();
    
    QMutexLocker locker(&m_mutex);
    
    for (auto it = m_trackers.begin(); it != m_trackers.end(); ++it) {
        if (it.value().tracker == sender) {
            it.value().completed = true;
            emit trackerCompleted(it.key());
            break;
        }
    }
    
    updateAggregatedProgress();
    checkCompletion();
}

void ProgressAggregator::onTrackerFailed(const QString& error)
{
    QObject* sender = QObject::sender();
    
    QMutexLocker locker(&m_mutex);
    
    for (auto it = m_trackers.begin(); it != m_trackers.end(); ++it) {
        if (it.value().tracker == sender) {
            it.value().failed = true;
            emit trackerFailed(it.key(), error);
            break;
        }
    }
    
    updateAggregatedProgress();
}

void ProgressAggregator::connectTracker(const QString& name, const TrackerInfo& info)
{
    if (info.isMultiStage) {
        auto* multiTracker = qobject_cast<MultiStageProgressTracker*>(info.tracker);
        if (multiTracker) {
            connect(multiTracker, &MultiStageProgressTracker::progressChanged,
                    this, &ProgressAggregator::onTrackerProgressChanged);
            connect(multiTracker, &MultiStageProgressTracker::statusChanged,
                    this, &ProgressAggregator::onTrackerStatusChanged);
            connect(multiTracker, &MultiStageProgressTracker::completed,
                    this, &ProgressAggregator::onTrackerCompleted);
            connect(multiTracker, &MultiStageProgressTracker::failed,
                    this, &ProgressAggregator::onTrackerFailed);
        }
    } else {
        auto* singleTracker = qobject_cast<ProgressTracker*>(info.tracker);
        if (singleTracker) {
            connect(singleTracker, &ProgressTracker::progressChanged,
                    this, &ProgressAggregator::onTrackerProgressChanged);
            connect(singleTracker, &ProgressTracker::statusChanged,
                    this, &ProgressAggregator::onTrackerStatusChanged);
            connect(singleTracker, &ProgressTracker::completed,
                    this, &ProgressAggregator::onTrackerCompleted);
            connect(singleTracker, &ProgressTracker::failed,
                    this, &ProgressAggregator::onTrackerFailed);
        }
    }
}

void ProgressAggregator::disconnectTracker(const QString& name, const TrackerInfo& info)
{
    if (info.tracker) {
        info.tracker->disconnect(this);
    }
}

void ProgressAggregator::updateAggregatedProgress()
{
    if (m_trackers.isEmpty()) {
        m_aggregatedProgress = ProgressInfo();
        return;
    }
    
    double totalWeight = 0.0;
    double completedWeight = 0.0;
    qint64 totalItems = 0;
    qint64 processedItems = 0;
    
    for (const auto& info : m_trackers) {
        totalWeight += info.weight;
        
        int percentage = 0;
        if (info.isMultiStage) {
            auto* multiTracker = qobject_cast<MultiStageProgressTracker*>(info.tracker);
            if (multiTracker) {
                percentage = multiTracker->overallPercentage();
            }
        } else {
            auto* singleTracker = qobject_cast<ProgressTracker*>(info.tracker);
            if (singleTracker) {
                percentage = singleTracker->percentage();
                totalItems += singleTracker->totalItems();
                processedItems += singleTracker->currentProgress().processedItems;
            }
        }
        
        completedWeight += info.weight * (percentage / 100.0);
    }
    
    m_aggregatedProgress.percentage = totalWeight > 0.0 ? 
        static_cast<int>((completedWeight / totalWeight) * 100.0) : 0;
    m_aggregatedProgress.totalItems = totalItems;
    m_aggregatedProgress.processedItems = processedItems;
    
    if (m_progressCallback) {
        m_progressCallback(m_aggregatedProgress);
    }
    
    emit progressChanged(m_aggregatedProgress);
}

void ProgressAggregator::checkCompletion()
{
    if (isCompleted()) {
        if (m_completionCallback) {
            m_completionCallback();
        }
        
        emit allTrackersCompleted();
    }
}

void ProgressAggregator::validateWeights()
{
    for (auto& info : m_trackers) {
        if (info.weight <= 0.0) {
            info.weight = 1.0;
        }
    }
}

#include "progresstracker.moc"