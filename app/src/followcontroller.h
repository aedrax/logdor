#ifndef FOLLOWCONTROLLER_H
#define FOLLOWCONTROLLER_H

#include <logdor/FileIdentity.h>
#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>
#include <logdor/LineIndexer.h>

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>

#include <memory>

/**
 * Follow mode's heartbeat: watches the current file for growth or rotation
 * while the shell keeps rendering its immutable snapshot.
 *
 * A 1 s timer guarantees a tick even where file watching is unreliable
 * (network mounts); a QFileSystemWatcher accelerates the common local case.
 * Each tick REOPENS the file (never remaps - a rotated file under mmap is a
 * SIGBUS hazard) and matches identity: Identical -> idle, Grown -> off-thread
 * extendLineIndex over the new tail, anything else -> rotated() so the shell
 * does a full reload. Ticks are skipped while an extend is in flight.
 *
 * emitted firstNewLine = the first line whose content changed: the old line
 * count, or one less when the old final line was unterminated (its content
 * grew - consumers must re-evaluate it).
 */
class Q_DECL_EXPORT FollowController : public QObject {
    Q_OBJECT
public:
    explicit FollowController(QObject* parent = nullptr);

    /// Begin following. @p source/@p index are the currently displayed
    /// snapshot; ticks reopen by path, so the caller keeps ownership.
    void start(const QString& path,
               std::shared_ptr<logdor::FileSource> source,
               std::shared_ptr<const logdor::LineIndex> index);
    void stop();
    bool isActive() const { return m_active; }

    /// The polling guarantee; the watcher only accelerates it.
    void setPollInterval(int ms) { m_timer.setInterval(ms); }

signals:
    /// The file grew: a fresh source + extended index, ready to swap in.
    void extended(std::shared_ptr<logdor::FileSource> source,
                  std::shared_ptr<const logdor::LineIndex> index,
                  qint64 firstNewLine);
    /// Identity changed (rotation/truncation): the shell must fully reload.
    void rotated();

private slots:
    void tick();
    void onExtendFinished();

private:
    QTimer m_timer;
    QFileSystemWatcher m_watcher;
    QFutureWatcher<logdor::IndexingResult> m_extendWatcher;

    QString m_path;
    std::shared_ptr<const logdor::LineIndex> m_index;
    logdor::FileIdentity m_identity;
    std::shared_ptr<logdor::FileSource> m_pendingSource; // extend in flight
    qint64 m_pendingFirstNewLine = 0;
    bool m_active = false;
    bool m_extendInFlight = false;
};

#endif // FOLLOWCONTROLLER_H
