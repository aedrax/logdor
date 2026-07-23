#include "legacybridge.h"

QList<LogEntry> materializeLegacyEntries(const logdor::FileSource& source,
                                         const logdor::LineIndex& index)
{
    Q_ASSERT(source.isContiguous());
    const char* base = source.data();

    QList<LogEntry> entries;
    entries.reserve(index.lineCount());
    for (qint64 line = 0; line < index.lineCount(); ++line) {
        entries.append(LogEntry(base + index.offsetOf(line),
                                size_t(index.rawLengthOf(line))));
    }
    return entries;
}
