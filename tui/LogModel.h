#pragma once

#include <logdor/FileSource.h>
#include <logdor/FilterScan.h>
#include <logdor/FormatParser.h>
#include <logdor/LineIndex.h>
#include <logdor/RowSet.h>

#include <QAbstractTableModel>
#include <QFuture>

#include <climits>
#include <memory>

// Table model for the TUI. It reads the file through the core and parses a
// line only when the view asks for it. It uses only QtCore.
class LogModel : public QAbstractTableModel {
public:
    LogModel(std::shared_ptr<logdor::FileSource> source,
             std::shared_ptr<const logdor::LineIndex> index,
             std::shared_ptr<const logdor::FormatParser> parser,
             QObject* parent = nullptr)
        : QAbstractTableModel(parent), m_source(std::move(source)),
          m_index(std::move(index)), m_parser(std::move(parser)),
          m_schema(m_parser->schema())
    {
    }

    // Start a filter scan on worker threads. An empty filter still hides
    // the format scaffolding lines (CSV header, W3C directives).
    QFuture<logdor::FilterScanResult> scan(logdor::LineFilter filter) const
    {
        if (m_parser->hasMetaLines()) {
            filter.extraPredicate = [p = m_parser](qint64 line, QByteArrayView raw) {
                return p->isDataLine(line, raw);
            };
        }
        return logdor::scanFilter(m_source, m_index, std::move(filter));
    }

    // Show only the lines in @p rows (the result of a scan).
    void setRows(logdor::RowSet rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        m_cachedLine = -1;
        endResetModel();
    }

    // The same colors as the GUI, as 0xRRGGBB. The GUI uses them as the row
    // background; QtTUI shows them as the text color, which is easier to
    // read in a terminal. 0 means no color.
    static int severityColor(logdor::Severity severity)
    {
        using logdor::Severity;
        switch (severity) {
        case Severity::Verbose: return 0x969696;
        case Severity::Debug: return 0x90ee90;
        case Severity::Info: return 0x87cefa;
        case Severity::Warning: return 0xffa500;
        case Severity::Error: return 0xff6347;
        case Severity::Fatal: return 0xba55d3;
        case Severity::None: break;
        }
        return 0;
    }

    qint64 lineCount() const { return m_index->lineCount(); }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : int(qMin<qint64>(m_rows.size(), INT_MAX));
    }

    int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_schema.size());
    }

    QVariant data(const QModelIndex& idx, int role = Qt::DisplayRole) const override
    {
        if (!idx.isValid())
            return {};
        if (role == Qt::DisplayRole)
            return parsed(idx.row()).fields.value(idx.column());
        if (role == Qt::ForegroundRole && m_parser->colorsBySeverity()) {
            if (const int color = severityColor(parsed(idx.row()).severity))
                return color;
        }
        return {};
    }

    QVariant headerData(int section, Qt::Orientation o,
                        int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || o != Qt::Horizontal)
            return {};
        return m_schema.value(section).name;
    }

private:
    // The view asks for each column of a row in sequence, so keep the last
    // parsed row.
    const logdor::ParsedRow& parsed(int row) const
    {
        const qint64 line = m_rows.sourceLine(row);
        if (line != m_cachedLine) {
            m_cachedRow = {};
            m_parser->parseLine(m_source->read(m_index->offsetOf(line),
                                               m_index->lengthOf(line)),
                                m_cachedRow);
            m_cachedLine = line;
        }
        return m_cachedRow;
    }

    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    std::shared_ptr<const logdor::FormatParser> m_parser;
    QList<logdor::FieldSchema> m_schema;
    logdor::RowSet m_rows;
    mutable qint64 m_cachedLine = -1;
    mutable logdor::ParsedRow m_cachedRow;
};
