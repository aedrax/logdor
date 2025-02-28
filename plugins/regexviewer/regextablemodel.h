#ifndef REGEXTABLEMODEL_H
#define REGEXTABLEMODEL_H

#include "../../app/src/plugininterface.h"
#include <QAbstractTableModel>
#include <QTableView>
#include <QString>
#include <QtPlugin>
#include <QItemSelection>
#include <QRegularExpression>
#include <QColor>

struct ValueWithColor {
    QVariant value;
    QColor color;

    ValueWithColor(const QVariant& v = QVariant(), const QColor& c = QColor())
        : value(v), color(c)
    {}
};

struct RegexFieldInfo : public FieldInfo {
    int groupNumber;  // The regex group number that maps to this field

    QList<ValueWithColor> coloredValues;  // Values with their associated colors

    RegexFieldInfo(const QString& n = QString(), DataType t = DataType::String, int group = 0)
        : groupNumber(group)
    {
        name = n;
        type = t;
    }

    void addValueWithColor(const QVariant& value, const QColor& color) {
        coloredValues.append(ValueWithColor(value, color));
        possibleValues.append(value);  // Keep base class list in sync
    }

    QColor getColorForValue(const QVariant& value) const {
        for (const ValueWithColor& vc : coloredValues) {
            if (vc.value == value) {
                return vc.color;
            }
        }
        return QColor();  // Return invalid color if no match
    }
};

struct RegexMatch {
    QString fullMatch;
    QStringList groups;
    int sourceRow;
};

class RegexTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit RegexTableModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    
    void setLogEntries(const QList<LogEntry>& entries);
    void setFilter(const FilterOptions& options);
    void setPattern(const QString& pattern);
    void setFieldInfo(const QList<RegexFieldInfo>& fields);
    int mapToSourceRow(int visibleRow) const { return m_matches[visibleRow].sourceRow; }
    QSet<int> getFilteredLines() const { return m_filteredLines; }

private:
    void updateMatches();
    QVariant convertToType(const QString& value, DataType type, int column) const;

    QList<LogEntry> m_entries;
    QString m_pattern;
    QRegularExpression m_regex;
    QList<RegexMatch> m_matches;
    QList<RegexFieldInfo> m_fields;
    QSet<int> m_filteredLines;
};

#endif // REGEXTABLEMODEL_H
