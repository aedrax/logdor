#ifndef CSVVIEWER_H
#define CSVVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>
#include <QtPlugin>

#include "csvtablemodel.h"

class CsvViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit CsvViewer(QObject* parent = nullptr);
    ~CsvViewer();

    // PluginInterface implementation
    QString name() const override { return tr("CSV Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer for CSV files that uses the first row as column headers."); }
    QWidget* widget() override { return m_container; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

private:
    void setupUi();

    QWidget* m_container;
    QVBoxLayout* m_layout;
    QTableView* m_tableView;
    CsvTableModel* m_model;
    FilterOptions m_filterOptions;
    QList<LogEntry> m_entries;
};

#endif // CSVVIEWER_H
