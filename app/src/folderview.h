#ifndef FOLDERVIEW_H
#define FOLDERVIEW_H

#include "logdorexport.h"

#include <QModelIndex>
#include <QTimer>
#include <QWidget>

class QFileSystemModel;
class QSortFilterProxyModel;
class QTreeView;

/**
 * Recursive file tree for "Open Folder": every file under the root, with
 * `*.logdor.json` annotation sidecars hidden and directories listed first.
 *
 * Selection follows with a short debounce so arrow-key browsing doesn't
 * thrash the indexer; Enter/double-click activates immediately.
 * selectNext()/selectPrevious() cycle through files in depth-first display
 * order with wrap-around. QFileSystemModel populates lazily, so the whole
 * tree is prefetched as directory listings arrive; navigation simply skips
 * branches that haven't landed yet.
 */
class LOGDOR_INTERFACE_EXPORT FolderView : public QWidget {
    Q_OBJECT
public:
    explicit FolderView(QWidget* parent = nullptr);

    void setFolder(const QString& dir);
    QString folder() const { return m_folder; }

    /// Highlights @p absolutePath without re-emitting fileActivated();
    /// ignores paths outside the folder (e.g. a plain Ctrl+O open).
    void setCurrentFile(const QString& absolutePath);

    void selectNext();
    void selectPrevious();

    /// All visible files in depth-first display order (directories first).
    QStringList files() const;

    QTreeView* treeView() const { return m_view; }

signals:
    void fileActivated(const QString& absolutePath);

private:
    void prefetch(const QModelIndex& sourceDir);
    void collectFiles(const QModelIndex& proxyParent, QModelIndexList& out) const;
    void step(int direction);
    // Suppresses repeats of the same path unless @p force (explicit
    // re-activation, e.g. double-click).
    void emitActivation(const QString& path, bool force);
    QModelIndex proxyRoot() const;
    QString pathOf(const QModelIndex& proxyIndex) const;

    QFileSystemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QTreeView* m_view = nullptr;
    QTimer* m_debounce = nullptr;
    QString m_folder;
    QString m_pendingCurrent; // setCurrentFile target not yet in the model
    QString m_lastActivated;
    bool m_syncing = false; // echo guard for programmatic selection
};

#endif // FOLDERVIEW_H
