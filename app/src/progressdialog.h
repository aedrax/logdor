#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QElapsedTimer>
#include <QTime>
#include <memory>

// Forward declarations
class ProgressTracker;
class MultiStageProgressTracker;
class ProgressAggregator;
struct ProgressInfo;

class ProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProgressDialog(QWidget* parent = nullptr);
    ~ProgressDialog();

    // Progress tracking setup
    void setProgressTracker(std::shared_ptr<ProgressTracker> tracker);
    void setMultiStageTracker(std::shared_ptr<MultiStageProgressTracker> tracker);
    void setProgressAggregator(std::shared_ptr<ProgressAggregator> aggregator);
    
    // Dialog configuration
    void setTitle(const QString& title);
    void setDescription(const QString& description);
    void setCancelable(bool cancelable);
    void setAutoClose(bool autoClose);
    void setMinimumDuration(int milliseconds);
    
    // Progress information
    int currentPercentage() const;
    QString currentStatus() const;
    bool isCancelled() const { return m_cancelled; }
    bool isCompleted() const { return m_completed; }
    
    // Show/hide with progress tracking
    void showProgress();
    void hideProgress();

signals:
    void cancelled();
    void completed();
    void progressChanged(int percentage);
    void statusChanged(const QString& status);

public slots:
    void updateProgress(const ProgressInfo& progress);
    void updateStatus(const QString& status);
    void onOperationCompleted();
    void onOperationCancelled();
    void onOperationFailed(const QString& error);

private slots:
    void onCancelClicked();
    void updateTimeDisplay();
    void checkMinimumDuration();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUI();
    void connectTracker();
    void disconnectTracker();
    void updateProgressDisplay(const ProgressInfo& progress);
    void updateTimeEstimate(const ProgressInfo& progress);
    void formatTimeRemaining(const QTime& time);
    void resetDialog();
    
    // UI components
    QVBoxLayout* m_mainLayout;
    QLabel* m_titleLabel;
    QLabel* m_descriptionLabel;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QLabel* m_detailsLabel;
    QLabel* m_timeLabel;
    QHBoxLayout* m_buttonLayout;
    QPushButton* m_cancelButton;
    QPushButton* m_closeButton;
    
    // Progress tracking
    std::shared_ptr<ProgressTracker> m_singleTracker;
    std::shared_ptr<MultiStageProgressTracker> m_multiTracker;
    std::shared_ptr<ProgressAggregator> m_aggregator;
    
    // Dialog state
    bool m_cancelled;
    bool m_completed;
    bool m_failed;
    bool m_cancelable;
    bool m_autoClose;
    int m_minimumDuration;
    
    // Time tracking
    QElapsedTimer m_showTimer;
    QTimer* m_updateTimer;
    QTimer* m_minimumTimer;
    
    // Display state
    QString m_currentTitle;
    QString m_currentDescription;
    int m_currentPercentage;
    QString m_currentStatus;
    
    // Constants
    static const int DEFAULT_UPDATE_INTERVAL_MS;
    static const int DEFAULT_MINIMUM_DURATION_MS;
    static const int DIALOG_MIN_WIDTH;
    static const int DIALOG_MIN_HEIGHT;
};

// Simple progress widget for embedding in other widgets
class ProgressWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProgressWidget(QWidget* parent = nullptr);
    ~ProgressWidget();

    // Progress tracking setup
    void setProgressTracker(std::shared_ptr<ProgressTracker> tracker);
    void setMultiStageTracker(std::shared_ptr<MultiStageProgressTracker> tracker);
    void setProgressAggregator(std::shared_ptr<ProgressAggregator> aggregator);
    
    // Widget configuration
    void setShowPercentage(bool show);
    void setShowStatus(bool show);
    void setShowTimeRemaining(bool show);
    void setShowCancelButton(bool show);
    void setCompactMode(bool compact);
    
    // Progress information
    int currentPercentage() const;
    QString currentStatus() const;
    bool isCancelled() const { return m_cancelled; }
    bool isCompleted() const { return m_completed; }
    
    // Visibility control
    void showProgress();
    void hideProgress();
    void setVisible(bool visible) override;

signals:
    void cancelled();
    void completed();
    void progressChanged(int percentage);
    void statusChanged(const QString& status);

public slots:
    void updateProgress(const ProgressInfo& progress);
    void updateStatus(const QString& status);
    void onOperationCompleted();
    void onOperationCancelled();
    void onOperationFailed(const QString& error);

private slots:
    void onCancelClicked();

private:
    void setupUI();
    void connectTracker();
    void disconnectTracker();
    void updateProgressDisplay(const ProgressInfo& progress);
    void updateLayout();
    
    // UI components
    QHBoxLayout* m_mainLayout;
    QVBoxLayout* m_progressLayout;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QLabel* m_percentageLabel;
    QLabel* m_timeLabel;
    QPushButton* m_cancelButton;
    
    // Progress tracking
    std::shared_ptr<ProgressTracker> m_singleTracker;
    std::shared_ptr<MultiStageProgressTracker> m_multiTracker;
    std::shared_ptr<ProgressAggregator> m_aggregator;
    
    // Widget state
    bool m_cancelled;
    bool m_completed;
    bool m_failed;
    bool m_showPercentage;
    bool m_showStatus;
    bool m_showTimeRemaining;
    bool m_showCancelButton;
    bool m_compactMode;
    
    // Display state
    int m_currentPercentage;
    QString m_currentStatus;
};

// Status bar progress indicator
class StatusBarProgress : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBarProgress(QWidget* parent = nullptr);
    ~StatusBarProgress();

    // Progress tracking setup
    void setProgressTracker(std::shared_ptr<ProgressTracker> tracker);
    void setMultiStageTracker(std::shared_ptr<MultiStageProgressTracker> tracker);
    void setProgressAggregator(std::shared_ptr<ProgressAggregator> aggregator);
    
    // Progress information
    int currentPercentage() const;
    QString currentStatus() const;
    bool isActive() const { return m_active; }
    
    // Control
    void startProgress();
    void stopProgress();
    void showProgress();
    void hideProgress();

signals:
    void cancelled();
    void completed();
    void clicked(); // For showing detailed progress dialog

public slots:
    void updateProgress(const ProgressInfo& progress);
    void updateStatus(const QString& status);
    void onOperationCompleted();
    void onOperationCancelled();
    void onOperationFailed(const QString& error);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    void setupUI();
    void connectTracker();
    void disconnectTracker();
    void updateDisplay();
    
    // Progress tracking
    std::shared_ptr<ProgressTracker> m_singleTracker;
    std::shared_ptr<MultiStageProgressTracker> m_multiTracker;
    std::shared_ptr<ProgressAggregator> m_aggregator;
    
    // Widget state
    bool m_active;
    bool m_completed;
    bool m_failed;
    
    // Display state
    int m_currentPercentage;
    QString m_currentStatus;
    
    // Constants
    static const int WIDGET_HEIGHT;
    static const int WIDGET_MIN_WIDTH;
    static const int PROGRESS_BAR_HEIGHT;
};

#endif // PROGRESSDIALOG_H