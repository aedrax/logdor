#include "folderview.h"

#include <QFileInfo>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

constexpr int kSelectionDebounceMs = 200;

/// Hides annotation sidecars; sorts directories first, then names.
class SidecarFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override
    {
        const auto* model = static_cast<QFileSystemModel*>(sourceModel());
        const QModelIndex index = model->index(sourceRow, 0, sourceParent);
        return model->isDir(index)
            || !model->fileName(index).endsWith(
                QLatin1String(".logdor.json"), Qt::CaseInsensitive);
    }

    bool lessThan(const QModelIndex& left,
                  const QModelIndex& right) const override
    {
        const auto* model = static_cast<QFileSystemModel*>(sourceModel());
        const bool leftDir = model->isDir(left);
        if (leftDir != model->isDir(right))
            return leftDir;
        return QString::compare(model->fileName(left), model->fileName(right),
                                Qt::CaseInsensitive)
            < 0;
    }
};

} // namespace

FolderView::FolderView(QWidget* parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_proxy(new SidecarFilterProxy(this))
    , m_view(new QTreeView(this))
    , m_debounce(new QTimer(this))
{
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_proxy->setSourceModel(m_model);
    m_proxy->sort(0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    m_view->setModel(m_proxy);
    m_view->setHeaderHidden(true);
    for (int column = 1; column < m_model->columnCount(); ++column)
        m_view->setColumnHidden(column, true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setUniformRowHeights(true);

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kSelectionDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        const QModelIndex current = m_view->currentIndex();
        if (current.isValid()
            && !m_model->isDir(m_proxy->mapToSource(current)))
            emitActivation(pathOf(current), false);
    });

    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                if (m_syncing || !current.isValid())
                    return;
                if (m_model->isDir(m_proxy->mapToSource(current)))
                    return;
                m_debounce->start();
            });

    connect(m_view, &QAbstractItemView::activated, this,
            [this](const QModelIndex& index) {
                if (m_model->isDir(m_proxy->mapToSource(index)))
                    return;
                m_debounce->stop();
                emitActivation(pathOf(index), true);
            });

    // The model lists lazily; walk each freshly loaded directory so the
    // whole tree converges and next/previous can see every file.
    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            [this](const QString&) {
                prefetch(m_model->index(m_folder));
                if (!m_pendingCurrent.isEmpty())
                    setCurrentFile(m_pendingCurrent);
            });
}

void FolderView::setFolder(const QString& dir)
{
    m_folder = QFileInfo(dir).canonicalFilePath();
    if (m_folder.isEmpty())
        m_folder = dir;
    m_pendingCurrent.clear();
    m_lastActivated.clear();
    const QModelIndex sourceRoot = m_model->setRootPath(m_folder);
    m_view->setRootIndex(m_proxy->mapFromSource(sourceRoot));
    prefetch(sourceRoot);
}

QModelIndex FolderView::proxyRoot() const
{
    return m_proxy->mapFromSource(m_model->index(m_folder));
}

QString FolderView::pathOf(const QModelIndex& proxyIndex) const
{
    return m_model->filePath(m_proxy->mapToSource(proxyIndex));
}

void FolderView::prefetch(const QModelIndex& sourceDir)
{
    if (!sourceDir.isValid())
        return;
    if (m_model->canFetchMore(sourceDir))
        m_model->fetchMore(sourceDir);
    for (int row = 0; row < m_model->rowCount(sourceDir); ++row) {
        const QModelIndex child = m_model->index(row, 0, sourceDir);
        if (m_model->isDir(child))
            prefetch(child);
    }
}

void FolderView::setCurrentFile(const QString& absolutePath)
{
    QString canonical = QFileInfo(absolutePath).canonicalFilePath();
    if (canonical.isEmpty())
        canonical = absolutePath;
    // QFileSystemModel indexes the whole filesystem; only track files that
    // actually live under the open folder.
    if (m_folder.isEmpty()
        || !canonical.startsWith(m_folder + QLatin1Char('/')))
        return;

    const QModelIndex source = m_model->index(canonical);
    if (!source.isValid()) {
        m_pendingCurrent = canonical; // retried on directoryLoaded
        return;
    }
    m_pendingCurrent.clear();
    m_lastActivated = canonical; // the debounce must not re-open it
    m_syncing = true;
    const QModelIndex proxy = m_proxy->mapFromSource(source);
    m_view->setCurrentIndex(proxy);
    m_view->scrollTo(proxy); // expands collapsed ancestors
    m_syncing = false;
}

void FolderView::collectFiles(const QModelIndex& proxyParent,
                              QModelIndexList& out) const
{
    for (int row = 0; row < m_proxy->rowCount(proxyParent); ++row) {
        const QModelIndex child = m_proxy->index(row, 0, proxyParent);
        if (m_model->isDir(m_proxy->mapToSource(child)))
            collectFiles(child, out);
        else
            out.append(child);
    }
}

void FolderView::step(int direction)
{
    if (m_folder.isEmpty())
        return;
    QModelIndexList files;
    collectFiles(proxyRoot(), files);
    if (files.isEmpty())
        return;

    const QModelIndex current = m_view->currentIndex();
    qsizetype position = files.indexOf(current);
    if (position < 0)
        position = direction > 0 ? files.size() - 1 : 0; // lands on first/last
    position = (position + direction + files.size()) % files.size();

    const QModelIndex target = files.at(position);
    m_debounce->stop();
    m_syncing = true;
    m_view->setCurrentIndex(target);
    m_view->scrollTo(target);
    m_syncing = false;
    emitActivation(pathOf(target), false);
}

QStringList FolderView::files() const
{
    QModelIndexList indexes;
    if (!m_folder.isEmpty())
        collectFiles(proxyRoot(), indexes);
    QStringList paths;
    paths.reserve(indexes.size());
    for (const QModelIndex& index : indexes)
        paths.append(pathOf(index));
    return paths;
}

void FolderView::selectNext()
{
    step(+1);
}

void FolderView::selectPrevious()
{
    step(-1);
}

void FolderView::emitActivation(const QString& path, bool force)
{
    if (!force && path == m_lastActivated)
        return;
    m_lastActivated = path;
    emit fileActivated(path);
}
