#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFile>
#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include <QSettings>
#include "pluginmanager.h"
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

private slots:
    void onActionOpenTriggered();
    void onFilterChanged();
    void onFocusFilterInput();

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
    bool openFile(const QString& fileName);
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
    QFile m_currentFile;
    const char* m_mappedFile;  // Track the mapped memory
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
