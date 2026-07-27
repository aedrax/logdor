#ifndef FOLDERSEARCHDOCK_H
#define FOLDERSEARCHDOCK_H

#include "logdorexport.h"

#include <logdor/GrepScan.h>

#include <QDockWidget>
#include <QFutureWatcher>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Folder-wide search: enumerate a folder (QDirIterator - the folder view's
 * lazily-populated model can't be trusted for completeness), stream
 * grepFolder results into a tree grouped by file, and let activation jump
 * into the file at the matched line. Sidecar annotation files are skipped.
 */
class LOGDOR_INTERFACE_EXPORT FolderSearchDock : public QDockWidget {
    Q_OBJECT
public:
    explicit FolderSearchDock(QWidget* parent = nullptr);

    void setFolder(const QString& path);
    void focusPattern();

signals:
    /// The user activated a match: open @p path and select @p line.
    void openRequested(const QString& path, qint64 line);
    /// Context menu: add @p path to the Merged Timeline.
    void addToTimelineRequested(const QString& path);

private:
    void startSearch();
    void onResultsReady(int beginIndex, int endIndex);

    QLineEdit* m_folderEdit = nullptr;
    QLineEdit* m_patternEdit = nullptr;
    QLineEdit* m_globEdit = nullptr;
    QCheckBox* m_regexCheck = nullptr;
    QCheckBox* m_caseCheck = nullptr;
    QPushButton* m_searchButton = nullptr;
    QTreeWidget* m_results = nullptr;
    QLabel* m_status = nullptr;
    QFutureWatcher<logdor::GrepFileResult> m_watcher;
    qint64 m_totalMatches = 0;
};

#endif // FOLDERSEARCHDOCK_H
