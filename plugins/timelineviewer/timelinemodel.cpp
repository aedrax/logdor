#include "timelinemodel.h"

#include "../../app/src/logtablemodel.h"

#include <QColor>
#include <QDateTime>
#include <QTimeZone>

using namespace logdor;

namespace {

/// The field shown in the Message column: the Message-hinted one, else the
/// last schema field (formats put the free text last).
int messageFieldOf(const QList<FieldSchema>& schema)
{
    for (int i = 0; i < schema.size(); ++i) {
        if (schema[i].hint == FieldHint::Message)
            return i;
    }
    return schema.size() - 1;
}

QString severityName(Severity severity)
{
    switch (severity) {
    case Severity::Verbose: return QStringLiteral("Verbose");
    case Severity::Debug: return QStringLiteral("Debug");
    case Severity::Info: return QStringLiteral("Info");
    case Severity::Warning: return QStringLiteral("Warning");
    case Severity::Error: return QStringLiteral("Error");
    case Severity::Fatal: return QStringLiteral("Fatal");
    case Severity::None: break;
    }
    return {};
}

} // namespace

TimelineModel::TimelineModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void TimelineModel::setMerged(
    std::vector<TimelineRow> order,
    QHash<qint32, std::shared_ptr<const TimelineFile>> files)
{
    beginResetModel();
    m_order = std::move(order);
    m_files = std::move(files);
    m_messageField.clear();
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it)
        m_messageField.insert(it.key(),
                              messageFieldOf(it.value()->parser->schema()));
    m_cache.clear();
    endResetModel();
}

int TimelineModel::rowForTime(qint64 utcMs) const
{
    const auto epochOf = [this](const TimelineRow& row) {
        qint64 ms = 0;
        const auto file = m_files.value(row.fileId);
        if (file)
            file->timeData->intAt(row.line, &ms);
        return ms;
    };
    size_t lo = 0, hi = m_order.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (epochOf(m_order[mid]) < utcMs)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == m_order.size())
        return -1;
    return m_descending ? int(m_order.size() - 1 - lo) : int(lo);
}

void TimelineModel::setDescending(bool descending)
{
    if (m_descending == descending)
        return;
    beginResetModel();
    m_descending = descending;
    endResetModel();
}

void TimelineModel::clearMerged()
{
    beginResetModel();
    m_order.clear();
    m_files.clear();
    m_messageField.clear();
    m_cache.clear();
    endResetModel();
}

int TimelineModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return int(qMin<qint64>(qint64(m_order.size()),
                            std::numeric_limits<int>::max()));
}

int TimelineModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 4;
}

const ParsedRow* TimelineModel::parsedRow(const TimelineFile& file,
                                          qint32 line) const
{
    const qint64 key = (qint64(file.fileId) << 32) | quint32(line);
    if (ParsedRow* cached = m_cache.object(key))
        return cached;

    const QByteArray raw = file.source->read(file.index->offsetOf(line),
                                             file.index->lengthOf(line));
    auto row = std::make_unique<ParsedRow>();
    file.parser->parseLine(QByteArrayView(raw), *row);
    const ParsedRow* result = row.get();
    m_cache.insert(key, row.release());
    return result;
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const TimelineRow row = m_order[orderIndex(index.row())];
    const auto file = m_files.value(row.fileId);
    if (!file)
        return {};

    const Severity severity = file->severity
            && size_t(row.line) < file->severity->size()
        ? Severity((*file->severity)[size_t(row.line)])
        : Severity::None;

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case FileColumn:
            return file->displayName;
        case TimeColumn: {
            qint64 ms = 0;
            if (file->timeData->intAt(row.line, &ms))
                return QDateTime::fromMSecsSinceEpoch(ms)
                    .toString(u"yyyy-MM-dd HH:mm:ss.zzz");
            return {}; // unreachable: rows without epochs are dropped
        }
        case SeverityColumn:
            return severityName(severity);
        case MessageColumn: {
            const ParsedRow* parsed = parsedRow(*file, row.line);
            const int field = m_messageField.value(row.fileId, -1);
            if (field >= 0 && field < parsed->fields.size())
                return parsed->fields[field];
            return {};
        }
        }
        return {};

    case Qt::BackgroundRole:
        if (file->parser->colorsBySeverity()) {
            const QColor color = LogTableModel::severityColor(severity);
            if (color.isValid())
                return color;
        }
        return {};

    case Qt::ForegroundRole:
        if (file->parser->colorsBySeverity()
            && LogTableModel::severityColor(severity).isValid())
            return QColor(Qt::black);
        return {};

    case Qt::ToolTipRole:
        return tr("%1, line %2").arg(file->path).arg(row.line + 1);
    }
    return {};
}

QVariant TimelineModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case FileColumn: return tr("File");
    case TimeColumn: return tr("Time");
    case SeverityColumn: return tr("Severity");
    case MessageColumn: return tr("Message");
    }
    return {};
}
