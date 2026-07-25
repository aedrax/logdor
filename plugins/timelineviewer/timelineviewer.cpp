#include "timelineviewer.h"

#include "../../app/src/formatcatalog.h"
#include "../../app/src/timesettings.h"

#include <logdor/ColumnScan.h>
#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

using namespace logdor;

namespace {

/// The column the merge keys on: first Timestamp-hinted field, else the
/// first DateTime field - the same resolution @time uses.
int timeColumnOf(const QList<FieldSchema>& schema)
{
    for (int i = 0; i < schema.size(); ++i) {
        if (schema[i].hint == FieldHint::Timestamp)
            return i;
    }
    for (int i = 0; i < schema.size(); ++i) {
        if (schema[i].type == FieldType::DateTime)
            return i;
    }
    return -1;
}

} // namespace

TimelineViewer::TimelineViewer(QObject* parent)
    : PluginInterface(parent)
{
    m_widget = new QWidget();

    auto* addButton = new QPushButton(tr("Add Files…"));
    auto* addCurrentButton = new QPushButton(tr("Add Current File"));
    auto* removeButton = new QPushButton(tr("Remove"));
    m_statusLabel = new QLabel(tr("Add log files to merge their events "
                                  "into one timeline."));
    m_statusLabel->setWordWrap(true);

    auto* toolbar = new QHBoxLayout();
    toolbar->addWidget(addButton);
    toolbar->addWidget(addCurrentButton);
    toolbar->addWidget(removeButton);
    toolbar->addStretch();

    m_fileList = new QListWidget();
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setMaximumHeight(120);

    auto* layout = new QVBoxLayout(m_widget);
    layout->addLayout(toolbar);
    layout->addWidget(m_fileList);
    layout->addWidget(m_statusLabel);
    layout->addStretch(); // A5 replaces this with the merged table view

    connect(addButton, &QPushButton::clicked,
            this, &TimelineViewer::addFilesDialog);
    connect(addCurrentButton, &QPushButton::clicked,
            this, &TimelineViewer::addCurrentFile);
    connect(removeButton, &QPushButton::clicked,
            this, &TimelineViewer::removeSelectedFiles);
    connect(m_fileList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                if (m_updatingList)
                    return;
                auto file = fileById(item->data(Qt::UserRole).toInt());
                if (!file)
                    return;
                const bool enabled = item->checkState() == Qt::Checked;
                if (file->enabled != enabled) {
                    file->enabled = enabled;
                    scheduleMerge();
                }
            });

    connect(&m_mergeWatcher, &QFutureWatcherBase::finished, this, [this]() {
        if (m_mergeWatcher.future().isCanceled())
            return;
        TimelineMergeResult result = m_mergeWatcher.result();
        m_order = std::move(result.order);
        m_mergeElapsedMs = result.elapsedMs;
        // droppedPerInput is positional over the inputs the merge was built
        // from: the enabled Ready files in list order at schedule time. A
        // list change between schedule and finish restarts the merge, so
        // the mapping below only ever sees a matching snapshot.
        size_t inputIndex = 0;
        for (const auto& file : m_files) {
            if (file->state == TimelineFile::State::Ready && file->enabled
                && inputIndex < result.droppedPerInput.size()) {
                file->droppedRows = result.droppedPerInput[inputIndex];
                ++inputIndex;
            }
        }
        refreshFileList();
        refreshStatus();
    });
}

TimelineViewer::~TimelineViewer()
{
    m_mergeWatcher.cancel();
    for (QFutureWatcherBase* watcher : std::as_const(m_pendingWatchers))
        watcher->cancel();
    delete m_widget;
}

void TimelineViewer::setCoreSource(std::shared_ptr<FileSource> source,
                                   std::shared_ptr<const LineIndex> index)
{
    // The timeline owns its own file set; the shell's current file only
    // feeds the "Add Current File" shortcut. Per the interface contract the
    // pointers are not retained.
    m_currentFilePath = source && index ? source->filePath() : QString();
    Q_UNUSED(index)
}

void TimelineViewer::setFilter(const FilterOptions& options)
{
    m_lastFilter = options; // applied to per-file row sets in Phase A6
}

void TimelineViewer::addFilesDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        m_widget, tr("Add Files to Timeline"), QString(),
        tr("Log files (*.log *.txt *.json);;All files (*)"));
    for (const QString& path : paths)
        addFile(path);
}

void TimelineViewer::addCurrentFile()
{
    if (!m_currentFilePath.isEmpty())
        addFile(m_currentFilePath);
}

void TimelineViewer::removeSelectedFiles()
{
    QSet<qint32> removed;
    const auto selected = m_fileList->selectedItems();
    for (const QListWidgetItem* item : selected)
        removed.insert(item->data(Qt::UserRole).toInt());
    if (removed.isEmpty())
        return;
    m_files.removeIf([&removed](const std::shared_ptr<TimelineFile>& file) {
        return removed.contains(file->fileId);
    });
    refreshFileList();
    scheduleMerge();
}

void TimelineViewer::addFile(const QString& path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (const auto& file : std::as_const(m_files)) {
        if (file->path == canonical)
            return; // already in the timeline
    }

    auto file = std::make_shared<TimelineFile>();
    file->fileId = m_nextFileId++;
    file->path = canonical.isEmpty() ? path : canonical;
    file->displayName = QFileInfo(path).fileName();
    m_files.append(file);

    QString error;
    file->source = FileSource::open(file->path, &error);
    if (!file->source) {
        failFile(file, error.isEmpty() ? tr("cannot open file") : error);
        return;
    }
    startIndexing(file);
    refreshFileList();
}

std::shared_ptr<TimelineFile> TimelineViewer::fileById(qint32 fileId) const
{
    for (const auto& file : m_files) {
        if (file->fileId == fileId)
            return file;
    }
    return nullptr;
}

void TimelineViewer::startIndexing(const std::shared_ptr<TimelineFile>& file)
{
    file->state = TimelineFile::State::Indexing;
    const qint32 fileId = file->fileId;
    watchFuture(buildLineIndex(file->source),
                [this, fileId](const IndexingResult& result) {
                    auto file = fileById(fileId);
                    if (!file) // removed while indexing
                        return;
                    file->index = result.index;
                    startExtraction(file);
                });
}

void TimelineViewer::startExtraction(const std::shared_ptr<TimelineFile>& file)
{
    if (m_parsers.isEmpty())
        m_parsers = loadAllParsers();

    const auto scores = detectFormat(*file->source, *file->index, m_parsers);
    if (!scores.isEmpty())
        file->parser = parserById(scores.front().parserId, m_parsers);
    if (!file->parser) {
        failFile(file, tr("empty file"));
        return;
    }
    file->timeColumn = timeColumnOf(file->parser->schema());
    if (file->timeColumn < 0) {
        failFile(file, tr("format \"%1\" has no timestamp field")
                           .arg(file->parser->displayName()));
        return;
    }

    file->state = TimelineFile::State::Extracting;
    refreshFileList();

    const qint32 fileId = file->fileId;
    const int timeColumn = file->timeColumn;
    watchFuture(
        extractColumns(file->source, file->index, file->parser, { timeColumn },
                       /*wantSeverity=*/true,
                       TimeSettings::instance().contextForFile(file->path)),
        [this, fileId, timeColumn](const ColumnScanResult& result) {
            auto file = fileById(fileId);
            if (!file)
                return;
            file->timeData = result.columns.value(timeColumn);
            file->severity = result.severity;
            if (!file->timeData || file->timeData->validIntCount() == 0) {
                failFile(file, tr("no parseable timestamps"));
                return;
            }
            if (file->timeData->isMonotonicTime()) {
                failFile(file, tr("uptime timestamps cannot be merged "
                                  "across files"));
                return;
            }
            file->state = TimelineFile::State::Ready;
            file->visibleRows = RowSet::all(file->index->lineCount());
            refreshFileList();
            scheduleMerge();
        });
}

void TimelineViewer::failFile(const std::shared_ptr<TimelineFile>& file,
                              const QString& reason)
{
    file->state = TimelineFile::State::Failed;
    file->error = reason;
    refreshFileList();
    refreshStatus();
}

void TimelineViewer::scheduleMerge()
{
    m_mergeWatcher.cancel();

    std::vector<TimelineInput> inputs;
    for (const auto& file : std::as_const(m_files)) {
        if (file->state == TimelineFile::State::Ready && file->enabled)
            inputs.push_back({ file->fileId, file->visibleRows,
                               file->timeData });
    }
    if (inputs.empty()) {
        m_order.clear();
        m_mergeElapsedMs = 0;
        refreshStatus();
        return;
    }
    m_mergeWatcher.setFuture(mergeTimeline(std::move(inputs)));
}

void TimelineViewer::refreshFileList()
{
    m_updatingList = true;
    m_fileList->clear();
    const QLocale locale;
    for (const auto& file : std::as_const(m_files)) {
        QString text = file->displayName;
        switch (file->state) {
        case TimelineFile::State::Indexing:
            text += tr(" — indexing…");
            break;
        case TimelineFile::State::Extracting:
            text += tr(" — reading timestamps…");
            break;
        case TimelineFile::State::Ready:
            text += tr(" — %1 · %2 lines")
                        .arg(file->parser->displayName(),
                             locale.toString(file->index->lineCount()));
            if (file->droppedRows > 0)
                text += tr(" (%1 without timestamps)")
                            .arg(locale.toString(file->droppedRows));
            break;
        case TimelineFile::State::Failed:
            text += tr(" — %1").arg(file->error);
            break;
        }
        auto* item = new QListWidgetItem(text, m_fileList);
        item->setData(Qt::UserRole, file->fileId);
        item->setToolTip(file->path);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(file->enabled ? Qt::Checked : Qt::Unchecked);
        if (file->state == TimelineFile::State::Failed)
            item->setForeground(Qt::gray);
    }
    m_updatingList = false;
}

void TimelineViewer::refreshStatus()
{
    if (m_files.isEmpty()) {
        m_statusLabel->setText(tr("Add log files to merge their events "
                                  "into one timeline."));
        return;
    }
    if (m_order.empty()) {
        m_statusLabel->setText(tr("No merged events yet."));
        return;
    }
    const QLocale locale;
    m_statusLabel->setText(tr("%1 events merged in %2 ms")
                               .arg(locale.toString(qint64(m_order.size())),
                                    locale.toString(m_mergeElapsedMs)));
}

template <typename T, typename Handler>
void TimelineViewer::watchFuture(QFuture<T> future, Handler onFinished)
{
    auto* watcher = new QFutureWatcher<T>(this);
    m_pendingWatchers.insert(watcher);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, onFinished]() {
                m_pendingWatchers.remove(watcher);
                watcher->deleteLater();
                if (!watcher->future().isCanceled())
                    onFinished(watcher->result());
            });
    watcher->setFuture(future);
}
