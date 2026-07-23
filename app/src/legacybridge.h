#ifndef LEGACYBRIDGE_H
#define LEGACYBRIDGE_H

#include "plugininterface.h"

#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>

#include <QList>

// Phase-1 bridge: materialize the legacy QList<LogEntry> plugin payload from
// the core index. Entries point into the FileSource's contiguous bytes and
// are byte-identical to the old MainWindow scan (lengths exclude '\n' but
// keep a trailing '\r'). Goes away in Phase 2 when models read the index
// directly. The source must be contiguous (mapped or ensureContiguous()'d).
QList<LogEntry> materializeLegacyEntries(const logdor::FileSource& source,
                                         const logdor::LineIndex& index);

#endif // LEGACYBRIDGE_H
