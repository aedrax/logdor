#ifndef SYSLOGVIEWER_H
#define SYSLOGVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTableView>
#include <QAbstractTableModel>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtPlugin>
#include <QAction>
#include <QMap>
#include <QComboBox>
#include <QSet>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>

#include "syslogentry.h"
#include "syslogtablemodel.h"

class SyslogViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit SyslogViewer(QObject* parent = nullptr);
    ~SyslogViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Syslog Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer for syslog files with facility and severity filtering."); }
    QWidget* widget() override { return m_container; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;
    QSet<QString> getUniqueFacilities() const;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void toggleSeverity(SyslogEntry::Severity severity, bool enabled);
    void toggleFacility(const QString& facility, bool enabled);
    void updateVisibleRows();
    void handleSort(int column, Qt::SortOrder order);
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

private:
    void setupUi();
    void parseLogLine(const QString& line);
    bool matchesFilter(const SyslogEntry& entry) const;
    void addFacilityLabel(const QString& facility);

    QWidget* m_container;
    QVBoxLayout* m_layout;
    QToolBar* m_toolbar;
    QTableView* m_tableView;
    SyslogTableModel* m_model;
    QMap<SyslogEntry::Severity, QAction*> m_severityActions;
    QComboBox* m_facilityComboBox;
    QSet<QString> m_selectedFacilities;
    QScrollArea* m_scrollArea;
    QFrame* m_facilitiesContainer;
    QHBoxLayout* m_facilitiesLayout;
    QMap<SyslogEntry::Severity, bool> m_severityFilters;
    FilterOptions m_filterOptions;
    QList<LogEntry> m_entries;
};

#endif // SYSLOGVIEWER_H
