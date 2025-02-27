#ifndef LOGCATVIEWER_H
#define LOGCATVIEWER_H

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

#include "logcatentry.h"
#include "logcattablemodel.h"

class LogcatViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit LogcatViewer(QObject* parent = nullptr);
    ~LogcatViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Logcat Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer for Android logcat logs with filtering and tag selection."); }
    QWidget* widget() override { return m_container; }
    bool setLogs(const QVector<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;
    QSet<QString> getUniqueTags() const;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void toggleLevel(LogcatEntry::Level level, bool enabled);
    void updateVisibleRows();
    void handleSort(int column, Qt::SortOrder order);
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

private:
    void setupUi();
    void parseLogLine(const QString& line);
    bool matchesFilter(const LogcatEntry& entry) const;
    void addTagLabel(const QString& tag);

    QWidget* m_container;
    QVBoxLayout* m_layout;
    QToolBar* m_toolbar;
    QTableView* m_tableView;
    LogcatTableModel* m_model;
    QMap<LogcatEntry::Level, QAction*> m_levelActions;
    QComboBox* m_tagComboBox;
    QSet<QString> m_selectedTags;
    QScrollArea* m_scrollArea;
    QFrame* m_tagsContainer;
    QHBoxLayout* m_tagsLayout;
    QMap<LogcatEntry::Level, bool> m_levelFilters;
    FilterOptions m_filterOptions;
    QVector<LogEntry> m_entries;
};

#endif // LOGCATVIEWER_H
