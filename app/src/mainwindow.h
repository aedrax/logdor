#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include <QSettings>
#include "pluginmanager.h"
#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>
#include <logdor/LineIndexer.h>
#include <memory>

#define FILTER_DEBOUNCE_TIMEOUT_MILLISECONDS 300

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QLineEdit;
class QSpinBox;
QT_END_NAMESPACE

// Forward declarations for background processing
class BackgroundTaskManager;
class ProgressDialog;
class StatusBarProgress;
class PluginProcessingTask;
struct TaskResult;
struct ProgressInfo;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    // Open a log file; returns true when indexing was started (completion is
    // asynchronous). Also used for files passed on the command line.
    bool openFile(const QString& fileName);

private slots:
    void onActionOpenTriggered();
    void onFilterChanged();
    void onFocusFilterInput();
    void onIndexingProgress(int permille);
    void onIndexingFinished();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Background processing slots
    void onBackgroundTaskStarted(const QString& taskId);
    void onBackgroundTaskCompleted(const QString& taskId, const TaskResult& result);
    void onBackgroundTaskCancelled(const QString& taskId);
    void onBackgroundTaskFailed(const QString& taskId, const QString& error);
    void onBackgroundTaskProgressChanged(const QString& taskId, const ProgressInfo& progress);
    void onProgressDialogCancelled();
    void onStatusBarProgressClicked();

private:
    void loadPlugins();
    // Materialize the legacy QList<LogEntry> from the core source if not
    // already done (heap-loads the file first in buffered mode).
    bool ensureLegacyEntries(QString* error);
    void saveSettings();
    void loadSettings();
    
    // Background processing methods
    void initializeBackgroundProcessing();
    void shutdownBackgroundProcessing();
    void processFileInBackground(const QString& fileName, const QList<LogEntry>& logEntries);
    void showProgressDialog(const QString& taskId, const QString& title);
    void hideProgressDialog();
    void updateStatusBarProgress(const QString& taskId);
    bool shouldUseBackgroundProcessing(const QString& fileName) const;

    Ui::MainWindow* ui;
    PluginManager* m_pluginManager;
    QMap<QString, PluginInterface*> m_activePlugins;
    QMap<QString, QDockWidget*> m_pluginDocks;
    QMap<QString, QAction*> m_pluginActions;
    QMenu* m_pluginsMenu;
    // Current file, owned by the core: the source outlives any background
    // consumer via shared_ptr; entries in m_logEntries point into its bytes.
    std::shared_ptr<logdor::FileSource> m_fileSource;
    std::shared_ptr<const logdor::LineIndex> m_lineIndex;
    QFutureWatcher<logdor::IndexingResult>* m_indexWatcher = nullptr;
    QString m_pendingFileName;
    QList<LogEntry> m_logEntries;
    FilterOptions m_filterOptions;
    
    // Filter controls
    QLineEdit* m_filterInput;
    QPushButton* m_caseSensitiveButton;
    QPushButton* m_invertFilterButton;
    QPushButton* m_queryModeButton;
    QPushButton* m_regexModeButton;
    QSpinBox* m_beforeSpinBox;
    QSpinBox* m_afterSpinBox;
    QTimer* m_filterTimer;
    
    // Background processing components
    std::unique_ptr<BackgroundTaskManager> m_backgroundTaskManager;
    std::unique_ptr<ProgressDialog> m_progressDialog;
    std::unique_ptr<StatusBarProgress> m_statusBarProgress;
    QString m_currentBackgroundTaskId;
    
    // Background processing configuration
    static const qint64 BACKGROUND_PROCESSING_THRESHOLD; // File size threshold for background processing
};

#endif // MAINWINDOW_H
