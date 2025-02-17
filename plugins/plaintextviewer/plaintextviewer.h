#ifndef PLAINTEXTVIEWER_H
#define PLAINTEXTVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTableView>
#include <QAbstractTableModel>
#include <QString>
#include <QtPlugin>

class PlainTextTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit PlainTextTableModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    
    void setLogEntries(const QVector<LogEntry>& entries);
    void applyFilter(const FilterOptions& options);

private:
    QVector<LogEntry> m_entries;
    QVector<int> m_visibleRows;  // Indices into m_entries for filtered view
};

class PlainTextViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit PlainTextViewer(QObject* parent = nullptr);
    ~PlainTextViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Plain Text Viewer"); }
    QWidget* widget() override { return m_tableView; }
    bool loadContent(const QVector<LogEntry>& content) override;
    void applyFilter(const FilterOptions& options) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    QTableView* m_tableView;
    PlainTextTableModel* m_model;
};

#endif // PLAINTEXTVIEWER_H
