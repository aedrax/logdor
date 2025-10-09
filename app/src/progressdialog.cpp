#include "progressdialog.h"
#include "progresstracker.h"
#include "backgroundtaskmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QStyle>
#include <QDebug>

// Constants
const int ProgressDialog::DEFAULT_UPDATE_INTERVAL_MS = 100;
const int ProgressDialog::DEFAULT_MINIMUM_DURATION_MS = 1000;
const int ProgressDialog::DIALOG_MIN_WIDTH = 400;
const int ProgressDialog::DIALOG_MIN_HEIGHT = 150;

const int StatusBarProgress::WIDGET_HEIGHT = 20;
const int StatusBarProgress::WIDGET_MIN_WIDTH = 200;
const int StatusBarProgress::PROGRESS_BAR_HEIGHT = 4;

// ProgressDialog implementation
ProgressDialog::ProgressDialog(QWidget* parent)
    : QDialog(parent)
    , m_cancelled(false)
    , m_completed(false)
    , m_failed(false)
    , m_cancelable(true)
    , m_autoClose(false)
    , m_minimumDuration(DEFAULT_MINIMUM_DURATION_MS)
    , m_currentPercentage(0)
{
    setupUI();
    
    // Setup update timer
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &ProgressDialog::updateTimeDisplay);
    m_updateTimer->start(DEFAULT_UPDATE_INTERVAL_MS);
    
    // Setup minimum duration timer
    m_minimumTimer = new QTimer(this);
    m_minimumTimer->setSingleShot(true);
    connect(m_minimumTimer, &QTimer::timeout, this, &ProgressDialog::checkMinimumDuration);
}

ProgressDialog::~ProgressDialog()
{
    disconnectTracker();
}

void ProgressDialog::setupUI()
{
    setWindowTitle("Progress");
    setModal(true);
    setMinimumSize(DIALOG_MIN_WIDTH, DIALOG_MIN_HEIGHT);
    
    // Main layout
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(10);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Title label
    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    m_titleLabel->setWordWrap(true);
    m_mainLayout->addWidget(m_titleLabel);
    
    // Description label
    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setWordWrap(true);
    m_mainLayout->addWidget(m_descriptionLabel);
    
    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_mainLayout->addWidget(m_progressBar);
    
    // Status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #666;");
    m_mainLayout->addWidget(m_statusLabel);
    
    // Details label (for additional information)
    m_detailsLabel = new QLabel(this);
    m_detailsLabel->setWordWrap(true);
    m_detailsLabel->setStyleSheet("color: #888; font-size: 11px;");
    m_mainLayout->addWidget(m_detailsLabel);
    
    // Time label
    m_timeLabel = new QLabel(this);
    m_timeLabel->setStyleSheet("color: #666; font-size: 11px;");
    m_timeLabel->setAlignment(Qt::AlignRight);
    m_mainLayout->addWidget(m_timeLabel);
    
    // Button layout
    m_buttonLayout = new QHBoxLayout();
    m_buttonLayout->addStretch();
    
    // Cancel button
    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &ProgressDialog::onCancelClicked);
    m_buttonLayout->addWidget(m_cancelButton);
    
    // Close button (hidden initially)
    m_closeButton = new QPushButton("Close", this);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    m_closeButton->setVisible(false);
    m_buttonLayout->addWidget(m_closeButton);
    
    m_mainLayout->addLayout(m_buttonLayout);
    
    // Initially hide some components
    m_descriptionLabel->setVisible(false);
    m_detailsLabel->setVisible(false);
    m_timeLabel->setVisible(false);
}

void ProgressDialog::setProgressTracker(std::shared_ptr<ProgressTracker> tracker)
{
    disconnectTracker();
    m_singleTracker = tracker;
    m_multiTracker.reset();
    m_aggregator.reset();
    connectTracker();
}

void ProgressDialog::setMultiStageTracker(std::shared_ptr<MultiStageProgressTracker> tracker)
{
    disconnectTracker();
    m_multiTracker = tracker;
    m_singleTracker.reset();
    m_aggregator.reset();
    connectTracker();
}

void ProgressDialog::setProgressAggregator(std::shared_ptr<ProgressAggregator> aggregator)
{
    disconnectTracker();
    m_aggregator = aggregator;
    m_singleTracker.reset();
    m_multiTracker.reset();
    connectTracker();
}

void ProgressDialog::setTitle(const QString& title)
{
    m_currentTitle = title;
    m_titleLabel->setText(title);
    m_titleLabel->setVisible(!title.isEmpty());
    setWindowTitle(title.isEmpty() ? "Progress" : title);
}

void ProgressDialog::setDescription(const QString& description)
{
    m_currentDescription = description;
    m_descriptionLabel->setText(description);
    m_descriptionLabel->setVisible(!description.isEmpty());
}

void ProgressDialog::setCancelable(bool cancelable)
{
    m_cancelable = cancelable;
    m_cancelButton->setVisible(cancelable && !m_completed && !m_failed);
}

void ProgressDialog::setAutoClose(bool autoClose)
{
    m_autoClose = autoClose;
}

void ProgressDialog::setMinimumDuration(int milliseconds)
{
    m_minimumDuration = milliseconds;
}

int ProgressDialog::currentPercentage() const
{
    return m_currentPercentage;
}

QString ProgressDialog::currentStatus() const
{
    return m_currentStatus;
}

void ProgressDialog::showProgress()
{
    resetDialog();
    m_showTimer.start();
    
    if (m_minimumDuration > 0) {
        m_minimumTimer->start(m_minimumDuration);
    } else {
        show();
    }
}

void ProgressDialog::hideProgress()
{
    hide();
    m_minimumTimer->stop();
}

void ProgressDialog::updateProgress(const ProgressInfo& progress)
{
    updateProgressDisplay(progress);
    updateTimeEstimate(progress);
    
    emit progressChanged(progress.percentage);
}

void ProgressDialog::updateStatus(const QString& status)
{
    m_currentStatus = status;
    m_statusLabel->setText(status);
    m_statusLabel->setVisible(!status.isEmpty());
    
    emit statusChanged(status);
}

void ProgressDialog::onOperationCompleted()
{
    m_completed = true;
    m_progressBar->setValue(100);
    m_statusLabel->setText("Completed successfully");
    
    m_cancelButton->setVisible(false);
    m_closeButton->setVisible(true);
    m_closeButton->setDefault(true);
    
    if (m_autoClose) {
        QTimer::singleShot(1000, this, &QDialog::accept);
    }
    
    emit completed();
}

void ProgressDialog::onOperationCancelled()
{
    m_cancelled = true;
    m_statusLabel->setText("Operation cancelled");
    m_statusLabel->setStyleSheet("color: #ff6600;");
    
    m_cancelButton->setVisible(false);
    m_closeButton->setVisible(true);
    m_closeButton->setDefault(true);
    
    emit cancelled();
}

void ProgressDialog::onOperationFailed(const QString& error)
{
    m_failed = true;
    m_statusLabel->setText(error.isEmpty() ? "Operation failed" : error);
    m_statusLabel->setStyleSheet("color: #cc0000;");
    
    m_cancelButton->setVisible(false);
    m_closeButton->setVisible(true);
    m_closeButton->setDefault(true);
}

void ProgressDialog::onCancelClicked()
{
    if (!m_cancelable || m_completed || m_cancelled || m_failed) {
        return;
    }
    
    m_cancelled = true;
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText("Cancelling...");
    
    // Notify trackers about cancellation
    if (m_singleTracker) {
        m_singleTracker->cancel("User cancelled operation");
    } else if (m_multiTracker) {
        m_multiTracker->cancel("User cancelled operation");
    }
    
    emit cancelled();
}

void ProgressDialog::updateTimeDisplay()
{
    if (m_showTimer.isValid()) {
        QTime elapsed = QTime(0, 0).addMSecs(static_cast<int>(m_showTimer.elapsed()));
        QString timeText = QString("Elapsed: %1").arg(elapsed.toString("mm:ss"));
        
        // Add estimated time remaining if available
        if (m_singleTracker) {
            QTime remaining = m_singleTracker->estimatedTimeRemaining();
            if (remaining.isValid() && remaining != QTime(0, 0)) {
                timeText += QString(" | Remaining: %1").arg(remaining.toString("mm:ss"));
            }
        }
        
        m_timeLabel->setText(timeText);
        m_timeLabel->setVisible(true);
    }
}

void ProgressDialog::checkMinimumDuration()
{
    if (!isVisible() && !m_completed && !m_cancelled && !m_failed) {
        show();
    }
}

void ProgressDialog::closeEvent(QCloseEvent* event)
{
    if (m_completed || m_cancelled || m_failed || !m_cancelable) {
        event->accept();
    } else {
        onCancelClicked();
        event->ignore();
    }
}

void ProgressDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_cancelable) {
        onCancelClicked();
    } else {
        QDialog::keyPressEvent(event);
    }
}

void ProgressDialog::connectTracker()
{
    if (m_singleTracker) {
        connect(m_singleTracker.get(), &ProgressTracker::progressChanged,
                this, &ProgressDialog::updateProgress);
        connect(m_singleTracker.get(), &ProgressTracker::statusChanged,
                this, &ProgressDialog::updateStatus);
        connect(m_singleTracker.get(), &ProgressTracker::completed,
                this, &ProgressDialog::onOperationCompleted);
        connect(m_singleTracker.get(), &ProgressTracker::cancelled,
                this, &ProgressDialog::onOperationCancelled);
        connect(m_singleTracker.get(), &ProgressTracker::failed,
                this, &ProgressDialog::onOperationFailed);
    } else if (m_multiTracker) {
        connect(m_multiTracker.get(), &MultiStageProgressTracker::progressChanged,
                this, &ProgressDialog::updateProgress);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::statusChanged,
                this, &ProgressDialog::updateStatus);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::completed,
                this, &ProgressDialog::onOperationCompleted);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::cancelled,
                this, &ProgressDialog::onOperationCancelled);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::failed,
                this, &ProgressDialog::onOperationFailed);
    } else if (m_aggregator) {
        connect(m_aggregator.get(), &ProgressAggregator::progressChanged,
                this, &ProgressDialog::updateProgress);
        connect(m_aggregator.get(), &ProgressAggregator::statusChanged,
                this, &ProgressDialog::updateStatus);
        connect(m_aggregator.get(), &ProgressAggregator::allTrackersCompleted,
                this, &ProgressDialog::onOperationCompleted);
    }
}

void ProgressDialog::disconnectTracker()
{
    if (m_singleTracker) {
        m_singleTracker->disconnect(this);
    }
    if (m_multiTracker) {
        m_multiTracker->disconnect(this);
    }
    if (m_aggregator) {
        m_aggregator->disconnect(this);
    }
}

void ProgressDialog::updateProgressDisplay(const ProgressInfo& progress)
{
    m_currentPercentage = progress.percentage;
    m_progressBar->setValue(progress.percentage);
    
    // Update details if available
    if (progress.totalItems > 0) {
        QString details = QString("Processing %1 of %2 items")
                         .arg(progress.processedItems)
                         .arg(progress.totalItems);
        
        if (progress.itemsPerSecond > 0) {
            details += QString(" (%1 items/sec)").arg(progress.itemsPerSecond, 0, 'f', 1);
        }
        
        m_detailsLabel->setText(details);
        m_detailsLabel->setVisible(true);
    }
}

void ProgressDialog::updateTimeEstimate(const ProgressInfo& progress)
{
    if (progress.estimatedRemaining.isValid() && progress.estimatedRemaining != QTime(0, 0)) {
        formatTimeRemaining(progress.estimatedRemaining);
    }
}

void ProgressDialog::formatTimeRemaining(const QTime& time)
{
    QString timeText;
    if (m_showTimer.isValid()) {
        QTime elapsed = QTime(0, 0).addMSecs(static_cast<int>(m_showTimer.elapsed()));
        timeText = QString("Elapsed: %1").arg(elapsed.toString("mm:ss"));
        
        if (time.isValid() && time != QTime(0, 0)) {
            timeText += QString(" | Remaining: %1").arg(time.toString("mm:ss"));
        }
    }
    
    m_timeLabel->setText(timeText);
    m_timeLabel->setVisible(!timeText.isEmpty());
}

void ProgressDialog::resetDialog()
{
    m_cancelled = false;
    m_completed = false;
    m_failed = false;
    m_currentPercentage = 0;
    m_currentStatus.clear();
    
    m_progressBar->setValue(0);
    m_statusLabel->clear();
    m_statusLabel->setStyleSheet("color: #666;");
    m_detailsLabel->clear();
    m_timeLabel->clear();
    
    m_cancelButton->setVisible(m_cancelable);
    m_cancelButton->setEnabled(true);
    m_cancelButton->setText("Cancel");
    m_closeButton->setVisible(false);
    
    m_statusLabel->setVisible(false);
    m_detailsLabel->setVisible(false);
    m_timeLabel->setVisible(false);
}

// ProgressWidget implementation
ProgressWidget::ProgressWidget(QWidget* parent)
    : QWidget(parent)
    , m_cancelled(false)
    , m_completed(false)
    , m_failed(false)
    , m_showPercentage(true)
    , m_showStatus(true)
    , m_showTimeRemaining(false)
    , m_showCancelButton(true)
    , m_compactMode(false)
    , m_currentPercentage(0)
{
    setupUI();
}

ProgressWidget::~ProgressWidget()
{
    disconnectTracker();
}

void ProgressWidget::setupUI()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(5);
    
    // Progress layout (vertical for status and progress bar)
    m_progressLayout = new QVBoxLayout();
    m_progressLayout->setContentsMargins(0, 0, 0, 0);
    m_progressLayout->setSpacing(2);
    
    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setMaximumHeight(20);
    m_progressLayout->addWidget(m_progressBar);
    
    // Status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #666; font-size: 11px;");
    m_progressLayout->addWidget(m_statusLabel);
    
    m_mainLayout->addLayout(m_progressLayout);
    
    // Percentage label
    m_percentageLabel = new QLabel("0%", this);
    m_percentageLabel->setMinimumWidth(30);
    m_percentageLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_percentageLabel);
    
    // Time label
    m_timeLabel = new QLabel(this);
    m_timeLabel->setStyleSheet("color: #666; font-size: 11px;");
    m_timeLabel->setMinimumWidth(60);
    m_mainLayout->addWidget(m_timeLabel);
    
    // Cancel button
    m_cancelButton = new QPushButton("×", this);
    m_cancelButton->setMaximumSize(20, 20);
    m_cancelButton->setStyleSheet("QPushButton { border: none; background: #ff6666; color: white; border-radius: 10px; font-weight: bold; }");
    connect(m_cancelButton, &QPushButton::clicked, this, &ProgressWidget::onCancelClicked);
    m_mainLayout->addWidget(m_cancelButton);
    
    updateLayout();
}

void ProgressWidget::setProgressTracker(std::shared_ptr<ProgressTracker> tracker)
{
    disconnectTracker();
    m_singleTracker = tracker;
    m_multiTracker.reset();
    m_aggregator.reset();
    connectTracker();
}

void ProgressWidget::setMultiStageTracker(std::shared_ptr<MultiStageProgressTracker> tracker)
{
    disconnectTracker();
    m_multiTracker = tracker;
    m_singleTracker.reset();
    m_aggregator.reset();
    connectTracker();
}

void ProgressWidget::setProgressAggregator(std::shared_ptr<ProgressAggregator> aggregator)
{
    disconnectTracker();
    m_aggregator = aggregator;
    m_singleTracker.reset();
    m_multiTracker.reset();
    connectTracker();
}

void ProgressWidget::setShowPercentage(bool show)
{
    m_showPercentage = show;
    updateLayout();
}

void ProgressWidget::setShowStatus(bool show)
{
    m_showStatus = show;
    updateLayout();
}

void ProgressWidget::setShowTimeRemaining(bool show)
{
    m_showTimeRemaining = show;
    updateLayout();
}

void ProgressWidget::setShowCancelButton(bool show)
{
    m_showCancelButton = show;
    updateLayout();
}

void ProgressWidget::setCompactMode(bool compact)
{
    m_compactMode = compact;
    updateLayout();
}

int ProgressWidget::currentPercentage() const
{
    return m_currentPercentage;
}

QString ProgressWidget::currentStatus() const
{
    return m_currentStatus;
}

void ProgressWidget::showProgress()
{
    m_cancelled = false;
    m_completed = false;
    m_failed = false;
    m_currentPercentage = 0;
    m_currentStatus.clear();
    
    m_progressBar->setValue(0);
    m_statusLabel->clear();
    m_percentageLabel->setText("0%");
    m_timeLabel->clear();
    
    show();
}

void ProgressWidget::hideProgress()
{
    hide();
}

void ProgressWidget::setVisible(bool visible)
{
    QWidget::setVisible(visible);
    updateLayout();
}

void ProgressWidget::updateProgress(const ProgressInfo& progress)
{
    updateProgressDisplay(progress);
    emit progressChanged(progress.percentage);
}

void ProgressWidget::updateStatus(const QString& status)
{
    m_currentStatus = status;
    m_statusLabel->setText(status);
    emit statusChanged(status);
}

void ProgressWidget::onOperationCompleted()
{
    m_completed = true;
    m_progressBar->setValue(100);
    m_percentageLabel->setText("100%");
    m_statusLabel->setText("Completed");
    m_cancelButton->setVisible(false);
    
    emit completed();
}

void ProgressWidget::onOperationCancelled()
{
    m_cancelled = true;
    m_statusLabel->setText("Cancelled");
    m_statusLabel->setStyleSheet("color: #ff6600; font-size: 11px;");
    m_cancelButton->setVisible(false);
    
    emit cancelled();
}

void ProgressWidget::onOperationFailed(const QString& error)
{
    m_failed = true;
    m_statusLabel->setText(error.isEmpty() ? "Failed" : error);
    m_statusLabel->setStyleSheet("color: #cc0000; font-size: 11px;");
    m_cancelButton->setVisible(false);
}

void ProgressWidget::onCancelClicked()
{
    if (!m_completed && !m_cancelled && !m_failed) {
        m_cancelled = true;
        m_cancelButton->setEnabled(false);
        
        // Notify trackers about cancellation
        if (m_singleTracker) {
            m_singleTracker->cancel("User cancelled operation");
        } else if (m_multiTracker) {
            m_multiTracker->cancel("User cancelled operation");
        }
        
        emit cancelled();
    }
}

void ProgressWidget::connectTracker()
{
    if (m_singleTracker) {
        connect(m_singleTracker.get(), &ProgressTracker::progressChanged,
                this, &ProgressWidget::updateProgress);
        connect(m_singleTracker.get(), &ProgressTracker::statusChanged,
                this, &ProgressWidget::updateStatus);
        connect(m_singleTracker.get(), &ProgressTracker::completed,
                this, &ProgressWidget::onOperationCompleted);
        connect(m_singleTracker.get(), &ProgressTracker::cancelled,
                this, &ProgressWidget::onOperationCancelled);
        connect(m_singleTracker.get(), &ProgressTracker::failed,
                this, &ProgressWidget::onOperationFailed);
    } else if (m_multiTracker) {
        connect(m_multiTracker.get(), &MultiStageProgressTracker::progressChanged,
                this, &ProgressWidget::updateProgress);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::statusChanged,
                this, &ProgressWidget::updateStatus);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::completed,
                this, &ProgressWidget::onOperationCompleted);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::cancelled,
                this, &ProgressWidget::onOperationCancelled);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::failed,
                this, &ProgressWidget::onOperationFailed);
    } else if (m_aggregator) {
        connect(m_aggregator.get(), &ProgressAggregator::progressChanged,
                this, &ProgressWidget::updateProgress);
        connect(m_aggregator.get(), &ProgressAggregator::statusChanged,
                this, &ProgressWidget::updateStatus);
        connect(m_aggregator.get(), &ProgressAggregator::allTrackersCompleted,
                this, &ProgressWidget::onOperationCompleted);
    }
}

void ProgressWidget::disconnectTracker()
{
    if (m_singleTracker) {
        m_singleTracker->disconnect(this);
    }
    if (m_multiTracker) {
        m_multiTracker->disconnect(this);
    }
    if (m_aggregator) {
        m_aggregator->disconnect(this);
    }
}

void ProgressWidget::updateProgressDisplay(const ProgressInfo& progress)
{
    m_currentPercentage = progress.percentage;
    m_progressBar->setValue(progress.percentage);
    
    if (m_showPercentage) {
        m_percentageLabel->setText(QString("%1%").arg(progress.percentage));
    }
    
    if (m_showTimeRemaining && progress.estimatedRemaining.isValid()) {
        m_timeLabel->setText(progress.estimatedRemaining.toString("mm:ss"));
    }
}

void ProgressWidget::updateLayout()
{
    m_percentageLabel->setVisible(m_showPercentage && !m_compactMode);
    m_statusLabel->setVisible(m_showStatus && !m_compactMode);
    m_timeLabel->setVisible(m_showTimeRemaining && !m_compactMode);
    m_cancelButton->setVisible(m_showCancelButton && !m_completed && !m_cancelled && !m_failed);
    
    if (m_compactMode) {
        m_progressBar->setTextVisible(m_showPercentage);
        m_progressBar->setMaximumHeight(16);
    } else {
        m_progressBar->setTextVisible(false);
        m_progressBar->setMaximumHeight(20);
    }
}

// StatusBarProgress implementation
StatusBarProgress::StatusBarProgress(QWidget* parent)
    : QWidget(parent)
    , m_active(false)
    , m_completed(false)
    , m_failed(false)
    , m_currentPercentage(0)
{
    setupUI();
    setFixedHeight(WIDGET_HEIGHT);
    setMinimumWidth(WIDGET_MIN_WIDTH);
    setCursor(Qt::PointingHandCursor);
}

StatusBarProgress::~StatusBarProgress()
{
    disconnectTracker();
}

void StatusBarProgress::setupUI()
{
    // No child widgets - we'll draw everything in paintEvent
}

void StatusBarProgress::setProgressTracker(std::shared_ptr<ProgressTracker> tracker)
{
    disconnectTracker();
    m_singleTracker = tracker;
    m_multiTracker.reset();
    m_aggregator.reset();
    connectTracker();
}

void StatusBarProgress::setMultiStageTracker(std::shared_ptr<MultiStageProgressTracker> tracker)
{
    disconnectTracker();
    m_multiTracker = tracker;
    m_singleTracker.reset();
    m_aggregator.reset();
    connectTracker();
}

void StatusBarProgress::setProgressAggregator(std::shared_ptr<ProgressAggregator> aggregator)
{
    disconnectTracker();
    m_aggregator = aggregator;
    m_singleTracker.reset();
    m_multiTracker.reset();
    connectTracker();
}

int StatusBarProgress::currentPercentage() const
{
    return m_currentPercentage;
}

QString StatusBarProgress::currentStatus() const
{
    return m_currentStatus;
}

void StatusBarProgress::startProgress()
{
    m_active = true;
    m_completed = false;
    m_failed = false;
    m_currentPercentage = 0;
    m_currentStatus.clear();
    showProgress();
}

void StatusBarProgress::stopProgress()
{
    m_active = false;
    hideProgress();
}

void StatusBarProgress::showProgress()
{
    setVisible(true);
    update();
}

void StatusBarProgress::hideProgress()
{
    setVisible(false);
}

void StatusBarProgress::updateProgress(const ProgressInfo& progress)
{
    m_currentPercentage = progress.percentage;
    m_currentStatus = progress.statusMessage;
    updateDisplay();
}

void StatusBarProgress::updateStatus(const QString& status)
{
    m_currentStatus = status;
    updateDisplay();
}

void StatusBarProgress::onOperationCompleted()
{
    m_completed = true;
    m_active = false;
    m_currentPercentage = 100;
    m_currentStatus = "Completed";
    updateDisplay();
    
    // Auto-hide after a delay
    QTimer::singleShot(2000, this, &StatusBarProgress::hideProgress);
    
    emit completed();
}

void StatusBarProgress::onOperationCancelled()
{
    m_active = false;
    m_currentStatus = "Cancelled";
    updateDisplay();
    
    // Auto-hide after a delay
    QTimer::singleShot(1000, this, &StatusBarProgress::hideProgress);
    
    emit cancelled();
}

void StatusBarProgress::onOperationFailed(const QString& error)
{
    m_failed = true;
    m_active = false;
    m_currentStatus = error.isEmpty() ? "Failed" : error;
    updateDisplay();
    
    // Auto-hide after a delay
    QTimer::singleShot(3000, this, &StatusBarProgress::hideProgress);
}

void StatusBarProgress::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}

void StatusBarProgress::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRect rect = this->rect();
    
    // Background
    painter.fillRect(rect, QColor(240, 240, 240));
    
    // Progress bar background
    QRect progressRect = rect.adjusted(2, rect.height() - PROGRESS_BAR_HEIGHT - 2, -2, -2);
    painter.fillRect(progressRect, QColor(220, 220, 220));
    
    // Progress bar fill
    if (m_currentPercentage > 0) {
        QRect fillRect = progressRect;
        fillRect.setWidth((fillRect.width() * m_currentPercentage) / 100);
        
        QColor fillColor = m_failed ? QColor(204, 0, 0) : 
                          m_completed ? QColor(0, 150, 0) : 
                          QColor(0, 120, 215);
        
        painter.fillRect(fillRect, fillColor);
    }
    
    // Status text
    if (!m_currentStatus.isEmpty()) {
        QRect textRect = rect.adjusted(4, 0, -4, -PROGRESS_BAR_HEIGHT - 4);
        painter.setPen(QColor(80, 80, 80));
        painter.setFont(QFont(font().family(), 9));
        
        QString displayText = m_currentStatus;
        QFontMetrics fm(painter.font());
        if (fm.horizontalAdvance(displayText) > textRect.width()) {
            displayText = fm.elidedText(displayText, Qt::ElideRight, textRect.width());
        }
        
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, displayText);
    }
    
    // Percentage text
    QString percentText = QString("%1%").arg(m_currentPercentage);
    QRect percentRect = rect.adjusted(-30, 0, -4, -PROGRESS_BAR_HEIGHT - 4);
    painter.setPen(QColor(100, 100, 100));
    painter.setFont(QFont(font().family(), 8));
    painter.drawText(percentRect, Qt::AlignRight | Qt::AlignVCenter, percentText);
}

QSize StatusBarProgress::sizeHint() const
{
    return QSize(WIDGET_MIN_WIDTH, WIDGET_HEIGHT);
}

void StatusBarProgress::connectTracker()
{
    if (m_singleTracker) {
        connect(m_singleTracker.get(), &ProgressTracker::progressChanged,
                this, &StatusBarProgress::updateProgress);
        connect(m_singleTracker.get(), &ProgressTracker::statusChanged,
                this, &StatusBarProgress::updateStatus);
        connect(m_singleTracker.get(), &ProgressTracker::completed,
                this, &StatusBarProgress::onOperationCompleted);
        connect(m_singleTracker.get(), &ProgressTracker::cancelled,
                this, &StatusBarProgress::onOperationCancelled);
        connect(m_singleTracker.get(), &ProgressTracker::failed,
                this, &StatusBarProgress::onOperationFailed);
    } else if (m_multiTracker) {
        connect(m_multiTracker.get(), &MultiStageProgressTracker::progressChanged,
                this, &StatusBarProgress::updateProgress);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::statusChanged,
                this, &StatusBarProgress::updateStatus);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::completed,
                this, &StatusBarProgress::onOperationCompleted);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::cancelled,
                this, &StatusBarProgress::onOperationCancelled);
        connect(m_multiTracker.get(), &MultiStageProgressTracker::failed,
                this, &StatusBarProgress::onOperationFailed);
    } else if (m_aggregator) {
        connect(m_aggregator.get(), &ProgressAggregator::progressChanged,
                this, &StatusBarProgress::updateProgress);
        connect(m_aggregator.get(), &ProgressAggregator::statusChanged,
                this, &StatusBarProgress::updateStatus);
        connect(m_aggregator.get(), &ProgressAggregator::allTrackersCompleted,
                this, &StatusBarProgress::onOperationCompleted);
    }
}

void StatusBarProgress::disconnectTracker()
{
    if (m_singleTracker) {
        m_singleTracker->disconnect(this);
    }
    if (m_multiTracker) {
        m_multiTracker->disconnect(this);
    }
    if (m_aggregator) {
        m_aggregator->disconnect(this);
    }
}

void StatusBarProgress::updateDisplay()
{
    update(); // Trigger repaint
}

#include "progressdialog.moc"