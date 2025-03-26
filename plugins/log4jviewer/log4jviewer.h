#ifndef LOG4JVIEWER_H
#define LOG4JVIEWER_H

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
#include <QLineEdit>

#include "log4jentry.h"
#include "log4jtablemodel.h"

class Log4jViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit Log4jViewer(QObject* parent = nullptr);
    ~Log4jViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Log4j Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer for Log4j logs with custom pattern support and filtering."); }
    QWidget* widget() override { return m_container; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;
    QSet<QString> getUniqueLoggers() const;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void toggleLevel(Log4jEntry::Level level, bool enabled);
    void updateVisibleRows();
    void handleSort(int column, Qt::SortOrder order);
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onPatternChanged();
    void onLoggerFilterChanged(const QString& logger);

private:
    void setupUi();
    void parseLogLine(const QString& line);
    bool matchesFilter(const Log4jEntry& entry) const;
    void addLoggerLabel(const QString& logger);

    QWidget* m_container;
    QVBoxLayout* m_layout;
    QToolBar* m_toolbar;
    QTableView* m_tableView;
    Log4jTableModel* m_model;
    QMap<Log4jEntry::Level, QAction*> m_levelActions;
    QComboBox* m_loggerComboBox;
    QSet<QString> m_selectedLoggers;
    QScrollArea* m_scrollArea;
    QFrame* m_loggersContainer;
    QHBoxLayout* m_loggersLayout;
    QMap<Log4jEntry::Level, bool> m_levelFilters;
    FilterOptions m_filterOptions;
    QList<LogEntry> m_entries;
    
    // Pattern input
    QLineEdit* m_patternInput;
    QString m_currentPattern;
};

#endif // LOG4JVIEWER_H
