#include "foldersearchdock.h"

#include <QCheckBox>
#include <QDirIterator>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace logdor;

namespace {

// Results are draggable as file URLs, so a row dropped onto the Merged
// Timeline (or any file-accepting target) adds that file.
class ResultsTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        QList<QUrl> urls;
        for (const QTreeWidgetItem* item : items) {
            const QString path = item->data(0, Qt::UserRole).toString();
            if (!path.isEmpty())
                urls.append(QUrl::fromLocalFile(path));
        }
        if (urls.isEmpty())
            return nullptr;
        auto* mime = new QMimeData;
        mime->setUrls(urls);
        return mime;
    }
};

} // namespace

FolderSearchDock::FolderSearchDock(QWidget* parent)
    : QDockWidget(tr("Folder Search"), parent)
{
    setObjectName(QStringLiteral("FolderSearchDock")); // windowState restore

    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);

    auto* grid = new QGridLayout();
    grid->addWidget(new QLabel(tr("Folder:"), body), 0, 0);
    m_folderEdit = new QLineEdit(body);
    grid->addWidget(m_folderEdit, 0, 1);
    auto* browseButton = new QPushButton(tr("..."), body);
    browseButton->setFixedWidth(32);
    grid->addWidget(browseButton, 0, 2);
    grid->addWidget(new QLabel(tr("Find:"), body), 1, 0);
    m_patternEdit = new QLineEdit(body);
    m_patternEdit->setPlaceholderText(tr("Search text or regex..."));
    grid->addWidget(m_patternEdit, 1, 1);
    m_searchButton = new QPushButton(tr("Search"), body);
    grid->addWidget(m_searchButton, 1, 2);
    grid->addWidget(new QLabel(tr("Files:"), body), 2, 0);
    m_globEdit = new QLineEdit(body);
    m_globEdit->setPlaceholderText(tr("*.log *.txt (empty = all files)"));
    grid->addWidget(m_globEdit, 2, 1);
    auto* toggles = new QHBoxLayout();
    m_regexCheck = new QCheckBox(tr("Regex"), body);
    m_caseCheck = new QCheckBox(tr("Case"), body);
    toggles->addWidget(m_regexCheck);
    toggles->addWidget(m_caseCheck);
    grid->addLayout(toggles, 2, 2);
    layout->addLayout(grid);

    m_results = new ResultsTree(body);
    m_results->setHeaderHidden(true);
    m_results->setUniformRowHeights(true);
    m_results->setDragEnabled(true);
    m_results->setDragDropMode(QAbstractItemView::DragOnly);
    m_results->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_results, /*stretch=*/1);

    m_status = new QLabel(body);
    layout->addWidget(m_status);
    setWidget(body);

    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Search Folder"), m_folderEdit->text());
        if (!dir.isEmpty())
            m_folderEdit->setText(dir);
    });
    connect(m_searchButton, &QPushButton::clicked,
            this, &FolderSearchDock::startSearch);
    connect(m_patternEdit, &QLineEdit::returnPressed,
            this, &FolderSearchDock::startSearch);
    connect(&m_watcher, &QFutureWatcherBase::resultsReadyAt,
            this, &FolderSearchDock::onResultsReady);
    connect(&m_watcher, &QFutureWatcherBase::finished, this, [this]() {
        if (m_watcher.future().isCanceled())
            return;
        const QLocale locale;
        m_status->setText(tr("%1 matches in %2 files")
                              .arg(locale.toString(m_totalMatches))
                              .arg(m_results->topLevelItemCount()));
    });
    connect(m_results, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item, int) {
                const QString path
                    = item->data(0, Qt::UserRole).toString();
                const qint64 line = item->data(0, Qt::UserRole + 1).toLongLong();
                if (!path.isEmpty() && line >= 0)
                    emit openRequested(path, line);
            });
    connect(m_results, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                QTreeWidgetItem* item = m_results->itemAt(pos);
                if (!item)
                    return;
                const QString path = item->data(0, Qt::UserRole).toString();
                if (path.isEmpty())
                    return;
                const qint64 line = item->data(0, Qt::UserRole + 1).toLongLong();
                QMenu menu(this);
                QAction* open = menu.addAction(tr("Open"));
                QAction* timeline
                    = menu.addAction(tr("Add to Merged Timeline"));
                QAction* chosen
                    = menu.exec(m_results->viewport()->mapToGlobal(pos));
                if (chosen == open)
                    emit openRequested(path, std::max<qint64>(line, 0));
                else if (chosen == timeline)
                    emit addToTimelineRequested(path);
            });
}

void FolderSearchDock::setFolder(const QString& path)
{
    m_folderEdit->setText(path);
}

void FolderSearchDock::focusPattern()
{
    m_patternEdit->setFocus();
    m_patternEdit->selectAll();
}

void FolderSearchDock::startSearch()
{
    m_watcher.cancel();
    m_results->clear();
    m_totalMatches = 0;

    const QString folder = m_folderEdit->text().trimmed();
    const QString pattern = m_patternEdit->text();
    if (folder.isEmpty() || pattern.isEmpty())
        return;

    // Enumerate up front: predictable ordering and no lazy-model gaps.
    QStringList nameFilters
        = m_globEdit->text().split(u' ', Qt::SkipEmptyParts);
    if (nameFilters.isEmpty())
        nameFilters << QStringLiteral("*");
    QStringList files;
    QDirIterator it(folder, nameFilters, QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QStringLiteral(".logdor.json")))
            continue; // annotation sidecars are not logs
        files.append(path);
    }
    m_status->setText(tr("Searching %1 files...").arg(files.size()));

    GrepQuery query;
    query.pattern = pattern;
    query.regexMode = m_regexCheck->isChecked();
    query.caseSensitivity = m_caseCheck->isChecked() ? Qt::CaseSensitive
                                                     : Qt::CaseInsensitive;
    m_watcher.setFuture(grepFolder(files, query));
}

void FolderSearchDock::onResultsReady(int beginIndex, int endIndex)
{
    const QDir base(m_folderEdit->text().trimmed());
    for (int i = beginIndex; i < endIndex; ++i) {
        const GrepFileResult result = m_watcher.resultAt(i);
        QString label = base.relativeFilePath(result.path);
        if (!result.error.isEmpty())
            label += tr(" — %1").arg(result.error);
        else if (result.skippedBinary)
            label += tr(" — binary, skipped");
        else
            label += tr(" — %1%2")
                         .arg(result.matches.size())
                         .arg(result.truncated ? QStringLiteral("+") : QString());
        auto* fileItem = new QTreeWidgetItem(m_results, { label });
        fileItem->setToolTip(0, result.path);
        // Path but line -1: draggable and context-menu-able, activation
        // (which needs a line) stays a no-op on the file row itself.
        fileItem->setData(0, Qt::UserRole, result.path);
        fileItem->setData(0, Qt::UserRole + 1, -1);
        for (const GrepMatch& match : result.matches) {
            auto* item = new QTreeWidgetItem(
                fileItem, { QStringLiteral("%1: %2")
                                .arg(match.line + 1)
                                .arg(match.excerpt.trimmed()) });
            item->setData(0, Qt::UserRole, result.path);
            item->setData(0, Qt::UserRole + 1, qint64(match.line));
        }
        m_totalMatches += result.matches.size();
        fileItem->setExpanded(m_results->topLevelItemCount() <= 4);
    }
}
