#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace logdor {

class FileSource;

/**
 * Content-based identity of a log file, for matching sidecar annotation
 * files to their log independent of path or name.
 *
 * Only the first min(size, 64 KiB) bytes are hashed and size is stored
 * OUTSIDE the hash, so identity survives the common case - the log was
 * appended to. Collisions are acceptable: identity only gates auto-load;
 * every annotation anchor is independently verified by its per-line hash.
 */
struct FileIdentity {
    quint64 size = 0;
    QByteArray prefixSha256; // lowercase hex of the first prefixLength bytes
    quint32 prefixLength = 0;

    bool isValid() const { return !prefixSha256.isEmpty(); }
};

enum class IdentityMatch {
    Identical, // same prefix hash, same size
    Grown,     // same prefix hash, file is larger (log was appended)
    Mismatch,
};

/// One bounded read; O(1) regardless of file size.
FileIdentity computeFileIdentity(const FileSource& source);

/**
 * Match a saved identity against the current file. Hashes the current file
 * over the SAVED prefix length, so a small file that later grew past 64 KiB
 * still matches as Grown.
 */
IdentityMatch matchIdentity(const FileIdentity& saved, const FileSource& current);

} // namespace logdor
