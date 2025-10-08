#include "regextablemodel.h"
#include <QtConcurrent/QtConcurrent>
#include <QDateTime>
#include <QBrush>

RegexViewerTableModel::RegexViewerTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int RegexViewerTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_matches.size();
}

int RegexViewerTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    // Line number + number of fields
    return 1 + m_fields.size();
}

QVariant RegexViewerTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_matches.size())
        return QVariant();

    const RegexMatch& match = m_matches[index.row()];

    if (role == Qt::DisplayRole || role == Qt::BackgroundRole) {
        if (index.column() == 0) {
            if (role == Qt::DisplayRole) {
                return match.sourceRow + 1; // Line number (1-based)
            }
            return QVariant(); // No background color for line numbers
        } else if (index.column() <= m_fields.size()) {
            const RegexFieldInfo& field = m_fields[index.column() - 1];
            // Get the group based on the field's group number
            QString value;
            if (field.groupNumber >= 0 && field.groupNumber < match.groups.size()) {
                value = match.groups[field.groupNumber];
            }

            if (role == Qt::DisplayRole) {
                return convertToType(value, field.type, index.column());
            } else if (role == Qt::BackgroundRole && !value.isEmpty()) {
                QVariant convertedValue = convertToType(value, field.type, index.column());
                if (convertedValue.isValid()) {
                    QColor color = field.getColorForValue(convertedValue);
                    if (color.isValid()) {
                        return QBrush(color);
                    }
                }
            }
        }
    }
    else if (role == Qt::FontRole) {
        return QFont("Monospace");
    }

    return QVariant();
}

QVariant RegexViewerTableModel::headerData(int section, Qt::Orientation orientation, int role) const
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

void RegexViewerTableModel::setLogEntries(const QList<LogEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    updateMatches();
    endResetModel();
}

void RegexViewerTableModel::setFilter(const FilterOptions& options)
{
    beginResetModel();
    // Clear filtered lines
    m_filteredLines.clear();
    
    if (!options.query.isEmpty()) {
        if (options.inRegexMode) {
            // Regex mode
            QRegularExpression::PatternOptions patternOptions = QRegularExpression::NoPatternOption;
            if (options.caseSensitivity == Qt::CaseInsensitive) {
                patternOptions |= QRegularExpression::CaseInsensitiveOption;
            }
            QRegularExpression filterRegex(options.query, patternOptions);
            
            for (int i = 0; i < m_entries.size(); i++) {
                bool matched = filterRegex.match(m_entries[i].getMessage()).hasMatch();
                // Add to filtered lines if:
                // - Not inverted and doesn't match, OR
                // - Inverted and does match
                if (matched == options.invertFilter) {
                    m_filteredLines.insert(i);
                }
            }
        } else {
            // Normal text search mode
            for (int i = 0; i < m_entries.size(); i++) {
                bool matched = m_entries[i].getMessage().contains(options.query, options.caseSensitivity);
                // Add to filtered lines if:
                // - Not inverted and doesn't match, OR
                // - Inverted and does match
                if (matched == options.invertFilter) {
                    m_filteredLines.insert(i);
                }
            }
        }
    }
    
    updateMatches();
    endResetModel();
}

void RegexViewerTableModel::setPattern(const QString& pattern)
{
    if (m_pattern != pattern) {
        beginResetModel();
        m_pattern = pattern;
        m_regex.setPattern(pattern);
        updateMatches();
        endResetModel();
    }
}

void RegexViewerTableModel::setFieldInfo(const QList<RegexFieldInfo>& fields)
{
    beginResetModel();
    m_fields = fields;
    endResetModel();
}

void RegexViewerTableModel::updateMatches()
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

QVariant RegexViewerTableModel::convertToType(const QString& value, DataType type, int column) const
{
    if (value.isEmpty()) {
        return QVariant();
    }

    QVariant convertedValue;
    switch (type) {
        case DataType::Integer: {
            bool ok;
            int intValue = value.toInt(&ok);
            convertedValue = ok ? QVariant(intValue) : QVariant(value);
            break;
        }
        case DataType::DateTime: {
            QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
            if (!dt.isValid()) {
                dt = QDateTime::fromString(value, Qt::RFC2822Date);
            }
            convertedValue = dt.isValid() ? QVariant(dt) : QVariant(value);
            break;
        }
        case DataType::String:
        default:
            convertedValue = QVariant(value);
            break;
    }

    // Check if this value matches one of the possible values for its field
    if (column > 0 && column <= m_fields.size()) {
        const RegexFieldInfo& field = m_fields[column - 1];
        if (!field.possibleValues.isEmpty()) {
            bool valueFound = false;
            for (const QVariant& possibleValue : field.possibleValues) {
                if (convertedValue == possibleValue) {
                    valueFound = true;
                    break;
                }
            }
            if (!valueFound) {
                // Value doesn't match any possible values, return empty
                return QVariant();
            }
        }
    }

    return convertedValue;
}
