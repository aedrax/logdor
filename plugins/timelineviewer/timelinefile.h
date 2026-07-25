#ifndef TIMELINEFILE_H
#define TIMELINEFILE_H

#include <logdor/FileSource.h>
#include <logdor/FormatParser.h>
#include <logdor/LineIndex.h>
#include <logdor/Query.h>
#include <logdor/RowSet.h>

#include <QString>

#include <memory>
#include <vector>

/**
 * One file the Merged Timeline owns. The timeline opens its own FileSource
 * per file (never borrowing the shell's current-file pointers, whose contract
 * requires dropping them on file switch) and walks each file through the
 * standard pipeline: open -> index -> detect -> extract time column -> Ready.
 * Failed files stay in the list with their reason so the analyst can see why
 * a file contributes nothing.
 */
struct TimelineFile {
    enum class State { Indexing, Extracting, Ready, Failed };

    qint32 fileId = 0; // stable per add; TimelineRow.fileId refers to this
    QString path;
    QString displayName;
    bool enabled = true;

    State state = State::Indexing;
    QString error; // Failed only

    std::shared_ptr<logdor::FileSource> source;
    std::shared_ptr<const logdor::LineIndex> index;
    std::shared_ptr<const logdor::FormatParser> parser;

    int timeColumn = -1;
    std::shared_ptr<const logdor::ColumnData> timeData;
    std::shared_ptr<const std::vector<quint8>> severity;

    /// Rows the merge sees: all lines until filtering (Phase A6) narrows it.
    logdor::RowSet visibleRows;
    /// Rows the last merge dropped for lacking a valid timestamp.
    qint64 droppedRows = 0;
};

#endif // TIMELINEFILE_H
