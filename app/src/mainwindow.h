#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include <QSettings>
#include "annotationhub.h"
#include "pluginmanager.h"
#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>
#include <logdor/LineIndexer.h>
#include <memory>

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

private:
    void loadPlugins();
    // Annotation persistence (sidecar next to the log, appdata fallback).
    QString annotationSidecarPath() const;
    QString annotationFallbackPath(const logdor::FileIdentity& identity) const;
    logdor::AnnotationSet loadAnnotationSidecars();
    void flushAnnotationSave();
    void updateNoteCount();
    void importAnnotations();
    void exportAnnotations();
    void saveSettings();
    void loadSettings();

    Ui::MainWindow* ui;
    PluginManager* m_pluginManager;
    QMap<QString, PluginInterface*> m_activePlugins;
    QMap<QString, QDockWidget*> m_pluginDocks;
    QMap<QString, QAction*> m_pluginActions;
    QMenu* m_pluginsMenu;
    // Current file, owned by the core; viewers hold shared_ptrs into it.
    std::shared_ptr<logdor::FileSource> m_fileSource;
    std::shared_ptr<const logdor::LineIndex> m_lineIndex;
    QFutureWatcher<logdor::IndexingResult>* m_indexWatcher = nullptr;
    QProgressDialog* m_indexProgress = nullptr;
    QString m_pendingFileName;
    QString m_currentFileName;
    AnnotationHub* m_annotationHub = nullptr;
    QTimer* m_annotationSaveTimer = nullptr;
    QLabel* m_noteCountLabel = nullptr;
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
};

#endif // MAINWINDOW_H
