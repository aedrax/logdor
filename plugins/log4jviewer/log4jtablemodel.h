#ifndef LOG4JTABLEMODEL_H
#define LOG4JTABLEMODEL_H

#include <QAbstractTableModel>
#include <QColor>
#include <QList>
#include "log4jentry.h"

class Log4jTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        LineNumber,
        Timestamp,
        Level,
        Thread,
        Logger,
        Message,
        MDC,
        ColumnCount
    };

    explicit Log4jTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void setEntries(const QList<Log4jEntry>& entries);
    void setPattern(const QString& pattern) { m_pattern = pattern; }
    QString pattern() const { return m_pattern; }
    const QList<Log4jEntry>& entries() const { return m_entries; }
    QSet<QString> getUniqueLoggers() const;

private:
    QList<Log4jEntry> m_entries;
    QString m_pattern;
    static QString columnName(Column column);
};

#endif // LOG4JTABLEMODEL_H
