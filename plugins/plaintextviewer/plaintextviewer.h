#ifndef PLAINTEXTVIEWER_H
#define PLAINTEXTVIEWER_H

#include "../../app/src/plugininterface.h"
#include "plaintexttablemodel.h"
#include <QTableView>
#include <QString>
#include <QtPlugin>
#include <QItemSelection>

class PlainTextViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit PlainTextViewer(QObject* parent = nullptr);
    ~PlainTextViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Plain Text Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A simple viewer for plain text logs."); }
    QWidget* widget() override { return m_tableView; }
    bool setLogs(const QVector<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

private:
    QTableView* m_tableView;
    PlainTextTableModel* m_model;
};

#endif // PLAINTEXTVIEWER_H
