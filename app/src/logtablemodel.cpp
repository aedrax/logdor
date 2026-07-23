#include "logtablemodel.h"

#include "annotationhub.h"

#include <QFont>
#include <QFontDatabase>

using namespace logdor;

LogTableModel::LogTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void LogTableModel::setSource(std::shared_ptr<FileSource> source,
                              std::shared_ptr<const LineIndex> index,
                              std::shared_ptr<const FormatParser> parser)
{
    beginResetModel();
    m_source = std::move(source);
    m_index = std::move(index);
    m_parser = std::move(parser);
    m_schema = m_parser ? m_parser->schema() : QList<FieldSchema>();
    m_rows = m_index ? RowSet::all(m_index->lineCount()) : RowSet();
    m_cache.clear();
    endResetModel();
}

void LogTableModel::setRowSet(RowSet rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    m_order.clear();
    m_inverse.clear();
    endResetModel();
}

void LogTableModel::setRowOrder(std::vector<qint32> order)
{
    Q_ASSERT(order.empty() || qint64(order.size()) == m_rows.size());
    beginResetModel();
    m_order = std::move(order);
    m_inverse.clear();
    if (!m_order.empty()) {
        m_inverse.resize(m_order.size());
        for (size_t viewRow = 0; viewRow < m_order.size(); ++viewRow)
            m_inverse[size_t(m_order[viewRow])] = qint32(viewRow);
    }
    endResetModel();
}

qint64 LogTableModel::sourceLineForRow(int row) const
{
    if (row < 0 || qint64(row) >= m_rows.size())
        return -1;
    const qint64 rowSetPos = m_order.empty() ? row : m_order[size_t(row)];
    return m_rows.sourceLine(rowSetPos);
}

int LogTableModel::rowForSourceLine(qint64 line) const
{
    const qint64 rowSetPos = m_rows.rowForSourceLine(line);
    if (rowSetPos < 0 || m_order.empty())
        return int(rowSetPos);
    return int(m_inverse[size_t(rowSetPos)]);
}

QColor LogTableModel::severityColor(Severity severity)
{
    switch (severity) {
    case Severity::Verbose: return QColor(150, 150, 150);
    case Severity::Debug: return QColor(144, 238, 144);
    case Severity::Info: return QColor(135, 206, 250);
    case Severity::Warning: return QColor(255, 165, 0);
    case Severity::Error: return QColor(255, 99, 71);
    case Severity::Fatal: return QColor(186, 85, 211);
    case Severity::None: break;
    }
    return QColor(); // invalid: no brush
}

void LogTableModel::setAnnotationHub(const AnnotationHub* hub)
{
    if (m_annotationHub)
        disconnect(m_annotationHub, nullptr, this, nullptr);
    m_annotationHub = hub;
    if (m_annotationHub) {
        connect(m_annotationHub, &AnnotationHub::annotationsChanged, this,
                [this]() {
                    if (rowCount() > 0)
                        emit dataChanged(
                            index(0, 0),
                            index(rowCount() - 1, columnCount() - 1),
                            { Qt::DecorationRole, Qt::ToolTipRole });
                });
    }
}

int LogTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return int(qMin<qint64>(m_rows.size(), std::numeric_limits<int>::max()));
}

int LogTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_schema.isEmpty() ? 0 : m_schema.size() + 1; // + "No."
}

const ParsedRow* LogTableModel::parsedRow(qint64 sourceLine) const
{
    if (ParsedRow* cached = m_cache.object(sourceLine))
        return cached;

    const QByteArray raw = m_source->read(m_index->offsetOf(sourceLine),
                                          m_index->lengthOf(sourceLine));
    auto row = std::make_unique<ParsedRow>();
    m_parser->parseLine(QByteArrayView(raw), *row);
    const ParsedRow* result = row.get();
    m_cache.insert(sourceLine, row.release());
    return result;
}

QVariant LogTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_source || !m_parser)
        return {};
    const qint64 line = sourceLineForRow(index.row());
    if (line < 0)
        return {};
    const int column = index.column();

    switch (role) {
    case Qt::DisplayRole:
        if (column == 0)
            return line + 1;
        if (column - 1 < m_schema.size())
            return parsedRow(line)->fields[column - 1];
        return {};

    case Qt::TextAlignmentRole:
        if (column == 0
            || (column - 1 < m_schema.size()
                && m_schema[column - 1].hint == FieldHint::Numeric))
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return {};

    case Qt::BackgroundRole:
        if (m_parser->colorsBySeverity()) {
            const QColor color = severityColor(parsedRow(line)->severity);
            if (color.isValid())
                return color;
        }
        return {};

    case Qt::ForegroundRole:
        if (m_parser->colorsBySeverity()
            && severityColor(parsedRow(line)->severity).isValid())
            return QColor(Qt::black);
        return {};

    case Qt::FontRole:
        if (column - 1 >= 0 && column - 1 < m_schema.size()
            && m_schema[column - 1].hint == FieldHint::Message)
            return QFontDatabase::systemFont(QFontDatabase::FixedFont);
        return {};

    case Qt::DecorationRole: {
        // Marker on the No. column only; one binary-search miss on the
        // (common) unannotated hot path.
        if (column != 0 || !m_annotationHub
            || !m_annotationHub->set().hasAnnotationAtLine(line))
            return {};
        const auto annotations = m_annotationHub->set().annotationsAtLine(line);
        QString color = annotations.first().color;
        if (color.isEmpty())
            color = QStringLiteral("#4a90d9"); // default accent
        QPixmap& marker = m_markerCache[color];
        if (marker.isNull()) {
            marker = QPixmap(8, 8);
            marker.fill(QColor(color));
        }
        return marker;
    }

    case Qt::ToolTipRole: {
        if (!m_annotationHub
            || !m_annotationHub->set().hasAnnotationAtLine(line))
            return {};
        QStringList parts;
        for (const auto& annotation :
             m_annotationHub->set().annotationsAtLine(line)) {
            QString entry = annotation.note;
            QStringList meta;
            if (!annotation.author.isEmpty())
                meta << annotation.author;
            if (annotation.modifiedAt.isValid())
                meta << annotation.modifiedAt.toLocalTime().toString(
                    QStringLiteral("yyyy-MM-dd hh:mm"));
            if (!annotation.tag.isEmpty())
                meta << QStringLiteral("#") + annotation.tag;
            if (!meta.isEmpty())
                entry += QStringLiteral("\n— ") + meta.join(QStringLiteral(", "));
            parts << entry;
        }
        return parts.join(QStringLiteral("\n\n"));
    }

    default:
        return {};
    }
}

QVariant LogTableModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    if (section == 0)
        return tr("No.");
    if (section - 1 < m_schema.size())
        return m_schema[section - 1].name;
    return {};
}
