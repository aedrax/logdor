#ifndef CLFVIEWER_H
#define CLFVIEWER_H

#include "../../app/src/plugininterface.h"
#include "clftablemodel.h"
#include <QTableView>
#include <QString>
#include <QtPlugin>
#include <QItemSelection>

class CLFViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit CLFViewer(QObject* parent = nullptr);
    ~CLFViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Common Log Format Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer for Common Log Format (CLF) logs."); }
    QWidget* widget() override { return m_tableView; }
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
    QTableView* m_tableView;
    CLFTableModel* m_model;
};

#endif // CLFVIEWER_H
