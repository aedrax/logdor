#ifndef PROGRESSTRACKER_H
#define PROGRESSTRACKER_H

#include <QObject>
#include <QMutex>
#include <QElapsedTimer>
#include <QTimer>
#include <QTime>
#include <QQueue>
#include <QAtomicInt>
#include <functional>

// Progress information structure
struct ProgressInfo {
    int percentage;           // 0-100
    QString statusMessage;    // Current operation description
    qint64 processedItems;   // Items processed so far
    qint64 totalItems;       // Total items to process
    QTime estimatedRemaining; // Estimated time to completion
    double itemsPerSecond;   // Processing rate
    
    ProgressInfo()
        : percentage(0)
        , processedItems(0)
        , totalItems(0)
        , itemsPerSecond(0.0)
    {}
    
    bool isValid() const { return totalItems > 0; }
    bool isComplete() const { return percentage >= 100; }
};

// Progress stage information
struct ProgressStage {
    QString name;
    QString description;
    double weight;        // Relative weight of this stage (0.0 - 1.0)
    int percentage;       // Current percentage within this stage (0-100)
    bool completed;       // Whether this stage is completed
    
    ProgressStage(const QString& stageName = QString(), 
                 const QString& stageDesc = QString(), 
                 double stageWeight = 1.0)
        : name(stageName)
        , description(stageDesc)
        , weight(stageWeight)
        , percentage(0)
        , completed(false)
    {}
    
    bool isValid() const { return !name.isEmpty() && weight > 0.0; }
};

// Performance metrics for progress tracking
struct ProgressMetrics {
    double itemsPerSecond;
    double bytesPerSecond;
    QTime estimatedTimeRemaining;
    QTime elapsedTime;
    qint64 totalItemsProcessed;
    qint64 totalBytesProcessed;
    
    ProgressMetrics()
        : itemsPerSecond(0.0)
        , bytesPerSecond(0.0)
        , totalItemsProcessed(0)
        , totalBytesProcessed(0)
    {}
    
    bool isValid() const { return totalItemsProcessed >= 0 && totalBytesProcessed >= 0; }
};

// Single-stage progress tracker
class ProgressTracker : public QObject
{
    Q_OBJECT

public:
    explicit ProgressTracker(QObject* parent = nullptr);
    explicit ProgressTracker(qint64 totalItems, const QString& description = QString(), QObject* parent = nullptr);
    ~ProgressTracker();

    // Configuration
    void setTotalItems(qint64 total);
    void setDescription(const QString& description);
    void setUpdateInterval(int milliseconds);
    
    qint64 totalItems() const { return m_totalItems; }
    QString description() const { return m_description; }
    int updateInterval() const { return m_updateInterval; }
    
    // Progress updates
    void setProgress(qint64 processedItems, const QString& statusMessage = QString());
    void incrementProgress(qint64 itemsDelta = 1, const QString& statusMessage = QString());
    void setProgressPercentage(int percentage, const QString& statusMessage = QString());
    void addProcessedBytes(qint64 bytes);
    
    // Status management
    void setStatusMessage(const QString& message);
    void complete(const QString& completionMessage = QString());
    void cancel(const QString& cancellationMessage = QString());
    void fail(const QString& errorMessage);
    
    // Progress information
    ProgressInfo currentProgress() const;
    ProgressMetrics currentMetrics() const;
    int percentage() const;
    QString statusMessage() const;
    bool isCompleted() const { return m_completed; }
    bool isCancelled() const { return m_cancelled; }
    bool isFailed() const { return m_failed; }
    
    // Performance metrics
    double itemsPerSecond() const;
    double bytesPerSecond() const;
    QTime estimatedTimeRemaining() const;
    QTime elapsedTime() const;
    
    // Callback registration
    void setProgressCallback(std::function<void(const ProgressInfo&)> callback);
    void setStatusCallback(std::function<void(const QString&)> callback);
    void setCompletionCallback(std::function<void()> callback);

signals:
    void progressChanged(const ProgressInfo& progress);
    void statusChanged(const QString& status);
    void completed();
    void cancelled();
    void failed(const QString& error);

private slots:
    void updateMetrics();

private:
    void initialize();
    void calculateMetrics();
    void emitProgressUpdate();
    void invokeCallbacks();
    
    // Configuration
    qint64 m_totalItems;
    QString m_description;
    int m_updateInterval;
    
    // Progress state
    QAtomicInt m_processedItems;
    qint64 m_processedBytes;
    QString m_currentStatus;
    bool m_completed;
    bool m_cancelled;
    bool m_failed;
    
    // Performance tracking
    QElapsedTimer m_elapsedTimer;
    QTimer* m_metricsTimer;
    QQueue<QPair<qint64, qint64>> m_recentProgress; // (timestamp, items)
    QQueue<QPair<qint64, qint64>> m_recentBytes;    // (timestamp, bytes)
    ProgressMetrics m_metrics;
    
    // Callbacks
    std::function<void(const ProgressInfo&)> m_progressCallback;
    std::function<void(const QString&)> m_statusCallback;
    std::function<void()> m_completionCallback;
    
    // Thread safety
    mutable QMutex m_mutex;
    
    // Constants
    static const int DEFAULT_UPDATE_INTERVAL_MS;
    static const int METRICS_HISTORY_SIZE;
    static const int MIN_SAMPLES_FOR_ESTIMATION;
};

// Multi-stage progress tracker for complex operations
class MultiStageProgressTracker : public QObject
{
    Q_OBJECT

public:
    explicit MultiStageProgressTracker(QObject* parent = nullptr);
    ~MultiStageProgressTracker();

    // Stage management
    void addStage(const QString& name, const QString& description, double weight = 1.0);
    void addStage(const ProgressStage& stage);
    void setCurrentStage(const QString& stageName);
    void setCurrentStage(int stageIndex);
    
    // Stage progress updates
    void setStageProgress(int percentage, const QString& statusMessage = QString());
    void setStageProgress(const QString& stageName, int percentage, const QString& statusMessage = QString());
    void completeCurrentStage(const QString& completionMessage = QString());
    void completeStage(const QString& stageName, const QString& completionMessage = QString());
    
    // Overall progress
    ProgressInfo currentProgress() const;
    int overallPercentage() const;
    QString currentStageDescription() const;
    QString currentStatusMessage() const;
    
    // Stage information
    QList<ProgressStage> stages() const;
    ProgressStage currentStage() const;
    int currentStageIndex() const { return m_currentStageIndex; }
    int stageCount() const;
    
    // Completion status
    bool isCompleted() const;
    bool isCancelled() const { return m_cancelled; }
    bool isFailed() const { return m_failed; }
    
    // Control operations
    void complete(const QString& completionMessage = QString());
    void cancel(const QString& cancellationMessage = QString());
    void fail(const QString& errorMessage);
    void reset();
    
    // Callback registration
    void setProgressCallback(std::function<void(const ProgressInfo&)> callback);
    void setStatusCallback(std::function<void(const QString&)> callback);
    void setStageChangedCallback(std::function<void(const QString&, int)> callback);
    void setCompletionCallback(std::function<void()> callback);

signals:
    void progressChanged(const ProgressInfo& progress);
    void statusChanged(const QString& status);
    void stageChanged(const QString& stageName, int stageIndex);
    void stageCompleted(const QString& stageName, int stageIndex);
    void completed();
    void cancelled();
    void failed(const QString& error);

private:
    void calculateOverallProgress();
    void emitProgressUpdate();
    void invokeCallbacks();
    void validateStageWeights();
    
    // Stage management
    QList<ProgressStage> m_stages;
    int m_currentStageIndex;
    QString m_currentStatusMessage;
    
    // Overall state
    bool m_cancelled;
    bool m_failed;
    QString m_errorMessage;
    
    // Performance tracking
    QElapsedTimer m_elapsedTimer;
    
    // Callbacks
    std::function<void(const ProgressInfo&)> m_progressCallback;
    std::function<void(const QString&)> m_statusCallback;
    std::function<void(const QString&, int)> m_stageChangedCallback;
    std::function<void()> m_completionCallback;
    
    // Thread safety
    mutable QMutex m_mutex;
};

// Progress aggregator for combining multiple progress trackers
class ProgressAggregator : public QObject
{
    Q_OBJECT

public:
    explicit ProgressAggregator(QObject* parent = nullptr);
    ~ProgressAggregator();

    // Tracker management
    void addTracker(const QString& name, ProgressTracker* tracker, double weight = 1.0);
    void addTracker(const QString& name, MultiStageProgressTracker* tracker, double weight = 1.0);
    void removeTracker(const QString& name);
    void clearTrackers();
    
    // Weight management
    void setTrackerWeight(const QString& name, double weight);
    double getTrackerWeight(const QString& name) const;
    
    // Aggregated progress information
    ProgressInfo aggregatedProgress() const;
    int overallPercentage() const;
    QString currentStatus() const;
    QStringList activeTrackerNames() const;
    
    // Completion status
    bool isCompleted() const;
    bool hasFailures() const;
    QStringList failedTrackerNames() const;
    
    // Callback registration
    void setProgressCallback(std::function<void(const ProgressInfo&)> callback);
    void setStatusCallback(std::function<void(const QString&)> callback);
    void setCompletionCallback(std::function<void()> callback);

signals:
    void progressChanged(const ProgressInfo& progress);
    void statusChanged(const QString& status);
    void trackerCompleted(const QString& trackerName);
    void trackerFailed(const QString& trackerName, const QString& error);
    void allTrackersCompleted();

private slots:
    void onTrackerProgressChanged(const ProgressInfo& progress);
    void onTrackerStatusChanged(const QString& status);
    void onTrackerCompleted();
    void onTrackerFailed(const QString& error);

private:
    struct TrackerInfo {
        QObject* tracker;
        double weight;
        bool isMultiStage;
        bool completed;
        bool failed;
        QString lastStatus;
        
        TrackerInfo(QObject* t = nullptr, double w = 1.0, bool multi = false)
            : tracker(t), weight(w), isMultiStage(multi), completed(false), failed(false)
        {}
    };
    
    void connectTracker(const QString& name, const TrackerInfo& info);
    void disconnectTracker(const QString& name, const TrackerInfo& info);
    void updateAggregatedProgress();
    void checkCompletion();
    void validateWeights();
    
    // Tracker storage
    QMap<QString, TrackerInfo> m_trackers;
    
    // Aggregated state
    ProgressInfo m_aggregatedProgress;
    QString m_currentStatus;
    
    // Callbacks
    std::function<void(const ProgressInfo&)> m_progressCallback;
    std::function<void(const QString&)> m_statusCallback;
    std::function<void()> m_completionCallback;
    
    // Thread safety
    mutable QMutex m_mutex;
};

#endif // PROGRESSTRACKER_H