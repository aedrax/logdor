#include "regextablemodel.h"
#include <QtConcurrent/QtConcurrent>
#include <QDateTime>

RegexTableModel::RegexTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int RegexTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_matches.size();
}

int RegexTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    // Line number + number of fields
    return 1 + m_fields.size();
}

QVariant RegexTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_matches.size())
        return QVariant();

    const RegexMatch& match = m_matches[index.row()];

    if (role == Qt::DisplayRole) {
        if (index.column() == 0) {
            return match.sourceRow + 1; // Line number (1-based)
        } else if (index.column() <= m_fields.size()) {
            const RegexFieldInfo& field = m_fields[index.column() - 1];
            // Get the group based on the field's group number
            QString value;
            if (field.groupNumber >= 0 && field.groupNumber < match.groups.size()) {
                value = match.groups[field.groupNumber];
            }
            return convertToType(value, field.type);
        }
    }
    else if (role == Qt::FontRole) {
        return QFont("Monospace");
    }

    return QVariant();
}

QVariant RegexTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        if (section == 0) {
            return tr("No.");
        } else if (section <= m_fields.size()) {
            return m_fields[section - 1].name;
        }
    }

    return QVariant();
}

void RegexTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    updateMatches();
    endResetModel();
}

void RegexTableModel::setFilter(const FilterOptions& options)
{
    beginResetModel();
    // Clear filtered lines
    m_filteredLines.clear();
    
    if (!options.query.isEmpty()) {
        for (int i = 0; i < m_entries.size(); i++) {
            bool matched = m_entries[i].getMessage().contains(options.query, options.caseSensitivity);
            if (!matched) {
                m_filteredLines.insert(i);
            }
        }
    }
    
    updateMatches();
    endResetModel();
}

void RegexTableModel::setPattern(const QString& pattern)
{
    if (m_pattern != pattern) {
        beginResetModel();
        m_pattern = pattern;
        m_regex.setPattern(pattern);
        updateMatches();
        endResetModel();
    }
}

void RegexTableModel::setFieldInfo(const QList<RegexFieldInfo>& fields)
{
    beginResetModel();
    m_fields = fields;
    endResetModel();
}

void RegexTableModel::updateMatches()
{
    m_matches.clear();
    
    if (m_pattern.isEmpty() || !m_regex.isValid()) {
        return;
    }

    for (int i = 0; i < m_entries.size(); i++) {
        if (m_filteredLines.contains(i)) {
            continue;
        }

        QString text = m_entries[i].getMessage();
        QRegularExpressionMatch match = m_regex.match(text);
        
        if (match.hasMatch()) {
            RegexMatch regexMatch;
            regexMatch.fullMatch = match.captured(0);
            regexMatch.sourceRow = i;
            
            // Store full match and all captured groups
            for (int j = 0; j < match.lastCapturedIndex(); ++j) {
                regexMatch.groups << match.captured(j + 1);
            }
            
            m_matches.append(regexMatch);
        }
    }
}

QVariant RegexTableModel::convertToType(const QString& value, DataType type) const
{
    switch (type) {
        case DataType::Integer: {
            bool ok;
            int intValue = value.toInt(&ok);
            return ok ? QVariant(intValue) : QVariant(value);
        }
        case DataType::DateTime: {
            QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
            if (!dt.isValid()) {
                dt = QDateTime::fromString(value, Qt::RFC2822Date);
            }
            return dt.isValid() ? QVariant(dt) : QVariant(value);
        }
        case DataType::String:
        default:
            return QVariant(value);
    }
}
