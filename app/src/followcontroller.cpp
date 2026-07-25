#include "followcontroller.h"

using namespace logdor;

FollowController::FollowController(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &FollowController::tick);
    // The watcher only accelerates the poll; a tick decides everything.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString&) { tick(); });
    connect(&m_extendWatcher, &QFutureWatcherBase::finished,
            this, &FollowController::onExtendFinished);
}

void FollowController::start(const QString& path,
                             std::shared_ptr<FileSource> source,
                             std::shared_ptr<const LineIndex> index)
{
    stop();
    if (!source || !index)
        return;
    m_path = path;
    m_index = std::move(index);
    m_identity = computeFileIdentity(*source);
    m_active = true;
    m_watcher.addPath(path);
    m_timer.start();
}

void FollowController::stop()
{
    m_active = false;
    m_timer.stop();
    m_extendWatcher.cancel();
    m_extendInFlight = false;
    m_pendingSource.reset();
    m_index.reset();
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
}

void FollowController::tick()
{
    if (!m_active || m_extendInFlight)
        return;

    // Always reopen, never remap: the old mapping stays valid for readers of
    // the old index (addressed only within its fileSize), and a rotated file
    // is met as a fresh handle instead of a SIGBUS.
    auto reopened = FileSource::open(m_path);
    if (!reopened) {
        // Mid-rotation the path can be briefly absent; treat as rotation and
        // let the shell's reload path deal with the state of the world.
        m_active = false;
        m_timer.stop();
        emit rotated();
        return;
    }

    switch (matchIdentity(m_identity, *reopened)) {
    case IdentityMatch::Identical:
        return; // idle
    case IdentityMatch::Grown: {
        m_extendInFlight = true;
        m_pendingSource = reopened;
        const qint64 oldCount = m_index->lineCount();
        m_pendingFirstNewLine
            = m_index->lastLineTerminated() ? oldCount : oldCount - 1;
        m_extendWatcher.setFuture(extendLineIndex(std::move(reopened), m_index));
        return;
    }
    case IdentityMatch::Mismatch:
        m_active = false;
        m_timer.stop();
        emit rotated();
        return;
    }
}

void FollowController::onExtendFinished()
{
    const bool wasCanceled = m_extendWatcher.future().isCanceled();
    m_extendInFlight = false;
    auto source = std::move(m_pendingSource);
    if (wasCanceled || !m_active)
        return;

    const IndexingResult result = m_extendWatcher.result();
    if (!result.index || result.index == m_index) {
        // Shrunk between reopen and extend: the next tick sees Mismatch.
        return;
    }
    m_index = result.index;
    emit extended(std::move(source), result.index, m_pendingFirstNewLine);
}
