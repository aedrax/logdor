#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include <QSettings>
#include "annotationhub.h"
#include "filtertoolbar.h"
#include "pluginmanager.h"
#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>
#include <logdor/LineIndexer.h>
#include <memory>

class FolderSearchDock;
class FolderView;
class FollowController;
class QLabel;
class QProgressDialog;

#define FILTER_DEBOUNCE_TIMEOUT_MILLISECONDS 300

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QLineEdit;
class QSpinBox;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    // Open a log file; returns true when indexing was started (completion is
    // asynchronous). Also used for files passed on the command line, where a
    // directory argument opens the folder view instead.
    bool openFile(const QString& fileName);

    // Show the folder dock listing every log under @p dir.
    void openFolder(const QString& dir);

    // Open @p fileName and, once its index lands, select @p line in every
    // viewer (the folder-search jump).
    void openFileAtLine(const QString& fileName, qint64 line);

private slots:
    void onActionOpenTriggered();
    void onActionOpenFolderTriggered();
    void onFilterChanged(const FilterOptions& options);
    void onFilterTermRequested(const QString& term);
    void onFocusFilterInput();
    void onIndexingProgress(int permille);
    void onIndexingFinished();
    void onAsyncOpenFinished();
    void onFollowToggled(bool on);
    void onFollowExtended(std::shared_ptr<logdor::FileSource> source,
                          std::shared_ptr<const logdor::LineIndex> index,
                          qint64 firstNewLine);
    void onFollowRotated();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    void loadPlugins();
    // Recents: MRU list of files and folders in QSettings ("recentItems").
    void addRecentItem(const QString& path);
    void rebuildRecentMenu();
    // Route a recents entry: folders open the folder view, files open.
    void openPath(const QString& path);
    // Annotation persistence (sidecar next to the log, appdata fallback).
    QString annotationSidecarPath() const;
    QString annotationFallbackPath(const logdor::FileIdentity& identity) const;
    logdor::AnnotationSet loadAnnotationSidecars();
    // Returns the path written, empty when nothing was dirty or writing failed.
    QString flushAnnotationSave();
    void saveAnnotationsNow();
    void saveAnnotationsAs();
    void updateNoteCount();
    void importAnnotations();
    void exportAnnotations();
    // Time-range picker: replace (never stack) the @time terms it injected
    // last time with @p terms, then re-apply the filter in query mode.
    void applyTimeRange(const QStringList& terms);
    void saveSettings();
    void loadSettings();
    // Debounced saveSettings, so layout survives exits that skip closeEvent
    // (SIGTERM on logout, Ctrl+C in a terminal, crashes).
    void scheduleSettingsSave();
    // Kick off buildLineIndex over m_fileSource/m_pendingFileName - the
    // second stage of openFile (first stage may be an async decompression).
    void startIndexingCurrentFile();
    // Per-file view state (filter + plugin states), kept for the app run so
    // cycling between files brings each one back the way it was left.
    void captureSession();
    void restoreSession(const QString& fileName);
    static QString sessionKey(const QString& fileName);

    Ui::MainWindow* ui;
    PluginManager* m_pluginManager;
    QMap<QString, PluginInterface*> m_activePlugins;
    QMap<QString, QDockWidget*> m_pluginDocks;
    QMap<QString, QAction*> m_pluginActions;
    QMenu* m_pluginsMenu;
    QMenu* m_recentMenu = nullptr;
    // Folder navigation, created lazily on the first Open Folder.
    FolderView* m_folderView = nullptr;
    QDockWidget* m_folderDock = nullptr;
    // Folder-wide search, created lazily on the first Ctrl+Shift+F.
    FolderSearchDock* m_searchDock = nullptr;
    void ensureSearchDock();
    qint64 m_pendingJumpLine = -1; // selected after the next index lands
    // Current file, owned by the core; viewers hold shared_ptrs into it.
    std::shared_ptr<logdor::FileSource> m_fileSource;
    std::shared_ptr<const logdor::LineIndex> m_lineIndex;
    QFutureWatcher<logdor::IndexingResult>* m_indexWatcher = nullptr;
    // Async open (decompression) preceding indexing for .gz files.
    QFutureWatcher<logdor::FileSource::AsyncOpenResult>* m_openWatcher = nullptr;
    QProgressDialog* m_indexProgress = nullptr;
    QString m_pendingFileName;
    QString m_currentFileName;
    AnnotationHub* m_annotationHub = nullptr;
    FollowController* m_followController = nullptr;
    QAction* m_followAction = nullptr;
    bool m_refollowAfterLoad = false; // rotation reload keeps following
    QTimer* m_annotationSaveTimer = nullptr;
    QTimer* m_settingsSaveTimer = nullptr;
    QLabel* m_noteCountLabel = nullptr;
    FilterOptions m_filterOptions;

    struct FileSession {
        FilterOptions filter;
        QStringList timeRangeTerms; // @time terms the picker injected
        QJsonObject pluginStates;   // keyed by plugin name
        logdor::FileIdentity identity; // restore gate across app runs
        qint64 lastUsed = 0;           // LRU eviction in sessions.json
    };
    QHash<QString, FileSession> m_sessions; // keyed by canonical file path
    QString sessionsFilePath() const;
    void loadSessionsFile();
    void saveSessionsFile() const;

    // The shared filter bar; MainWindow only fans its options out.
    FilterToolbar* m_filterToolbar;
};

#endif // MAINWINDOW_H
