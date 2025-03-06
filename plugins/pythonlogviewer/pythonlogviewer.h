#ifndef PYTHONLOGVIEWER_H
#define PYTHONLOGVIEWER_H

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

#include "pythonlogentry.h"
#include "pythonlogtablemodel.h"

class PythonLogViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit PythonLogViewer(QObject* parent = nullptr);
    ~PythonLogViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Python Log Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer for Python logs with filtering and module selection."); }
    QWidget* widget() override { return m_container; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;
    QSet<QString> getUniqueModules() const;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void toggleLevel(PythonLogEntry::Level level, bool enabled);
    void updateVisibleRows();
    void handleSort(int column, Qt::SortOrder order);
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

private:
    void setupUi();
    void parseLogLine(const QString& line);
    bool matchesFilter(const PythonLogEntry& entry) const;
    void addModuleLabel(const QString& module);

    QWidget* m_container;
    QVBoxLayout* m_layout;
    QToolBar* m_toolbar;
    QTableView* m_tableView;
    PythonLogTableModel* m_model;
    QMap<PythonLogEntry::Level, QAction*> m_levelActions;
    QComboBox* m_moduleComboBox;
    QSet<QString> m_selectedModules;
    QScrollArea* m_scrollArea;
    QFrame* m_modulesContainer;
    QHBoxLayout* m_modulesLayout;
    QMap<PythonLogEntry::Level, bool> m_levelFilters;
    FilterOptions m_filterOptions;
    QList<LogEntry> m_entries;
};

#endif // PYTHONLOGVIEWER_H
