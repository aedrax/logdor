#ifndef TIMELINEVIEWER_H
#define TIMELINEVIEWER_H

#include "../../app/src/plugininterface.h"
#include "timelinefile.h"

#include <logdor/TimelineMerge.h>

#include <QFutureWatcher>
#include <QSet>
#include <QtPlugin>

#include <memory>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QTableView;
class TimelineModel;

/**
 * Merged Timeline: the analyst adds N log files - same or different formats -
 * and sees their rows interleaved in one time-ascending order. Owns its own
 * file set independent of the shell's current file; each file runs the
 * standard per-file pipeline and re-merges on any change.
 */
class TimelineViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit TimelineViewer(QObject* parent = nullptr);
    ~TimelineViewer();

    QString name() const override { return tr("Merged Timeline"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override
    {
        return tr("Merge events from multiple log files into one "
                  "time-sorted view.");
    }
    QWidget* widget() override { return m_widget; }

    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override;
    void setFilter(const FilterOptions& options) override;

private slots:
    void addFilesDialog();
    void addCurrentFile();
    void removeSelectedFiles();

private:
    void addFile(const QString& path);
    std::shared_ptr<TimelineFile> fileById(qint32 fileId) const;
    void startIndexing(const std::shared_ptr<TimelineFile>& file);
    void startExtraction(const std::shared_ptr<TimelineFile>& file);
    void applyFilterToFile(const std::shared_ptr<TimelineFile>& file);
    void failFile(const std::shared_ptr<TimelineFile>& file,
                  const QString& reason);
    void scheduleMerge();
    void refreshFileList();
    void refreshStatus();

    template <typename T, typename Handler>
    void watchFuture(QFuture<T> future, Handler onFinished);

    QWidget* m_widget = nullptr;
    QListWidget* m_fileList = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTableView* m_table = nullptr;
    TimelineModel* m_model = nullptr;

    QList<std::shared_ptr<TimelineFile>> m_files;
    qint32 m_nextFileId = 0;
    QString m_currentFilePath; // the shell's current file, for "Add Current"
    FilterOptions m_lastFilter;

    QList<std::shared_ptr<const logdor::FormatParser>> m_parsers;

    QFutureWatcher<logdor::TimelineMergeResult> m_mergeWatcher;
    qint64 m_mergeElapsedMs = 0;
    int m_filterGeneration = 0; // discards stale per-file scan results
    QSet<QFutureWatcherBase*> m_pendingWatchers;
    bool m_updatingList = false;
};

#endif // TIMELINEVIEWER_H
