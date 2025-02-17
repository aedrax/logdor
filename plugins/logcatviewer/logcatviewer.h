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

class TagLabel : public QFrame {
    Q_OBJECT
public:
    TagLabel(const QString& tag, QWidget* parent = nullptr);
    QString tag() const { return m_tag; }

signals:
    void removed();

private:
    QString m_tag;
    QHBoxLayout* m_layout;
    QLabel* m_label;
    QPushButton* m_removeButton;
};

class LogcatEntry {
public:
    enum class Level {
        Verbose,
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };

    QString timestamp;
    QString pid;
    QString tid;  // Thread ID
    Level level;
    QString tag;
    QString message;

    static Level parseLevel(const QChar& levelChar) {
        switch (levelChar.toLatin1()) {
            case 'V': return Level::Verbose;
            case 'D': return Level::Debug;
            case 'I': return Level::Info;
            case 'W': return Level::Warning;
            case 'E': return Level::Error;
            case 'F': return Level::Fatal;
            default: return Level::Info;
        }
    }

    static QString levelToString(Level level) {
        switch (level) {
            case Level::Verbose: return "Verbose";
            case Level::Debug: return "Debug";
            case Level::Info: return "Info";
            case Level::Warning: return "Warning";
            case Level::Error: return "Error";
            case Level::Fatal: return "Fatal";
            default: return "Unknown";
        }
    }

    static QColor levelColor(Level level) {
        switch (level) {
            case Level::Verbose: return QColor(150, 150, 150);  // Light Gray
            case Level::Debug: return QColor(144, 238, 144);    // Light Green
            case Level::Info: return QColor(135, 206, 250);     // Light Blue
            case Level::Warning: return QColor(255, 165, 0);    // Orange
            case Level::Error: return QColor(255, 99, 71);      // Tomato Red
            case Level::Fatal: return QColor(186, 85, 211);     // Medium Purple
            default: return QColor(255, 255, 255);              // White
        }
    }
};

class LogcatTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit LogcatTableModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    
    void setLogEntries(const QVector<LogEntry>& entries);
    void applyFilter(const QString& query, const QSet<QString>& tags, 
                    const QMap<LogcatEntry::Level, bool>& levelFilters,
                    int contextBefore, int contextAfter);
    QSet<QString> getUniqueTags() const;
    LogcatEntry logEntryToLogcatEntry(const LogEntry& entry) const;
    int mapToSourceRow(int visibleRow) const { return m_visibleRows[visibleRow]; }

private:
    bool matchesFilter(const LogcatEntry& entry, const QString& query, 
                      const QSet<QString>& tags,
                      const QMap<LogcatEntry::Level, bool>& levelFilters) const;

    QVector<LogEntry> m_entries;
    QVector<int> m_visibleRows;  // Indices into m_entries for filtered view
    int m_sortColumn{0};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
};

class LogcatViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit LogcatViewer(QObject* parent = nullptr);
    ~LogcatViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Logcat Viewer"); }
    QWidget* widget() override { return m_container; }
    bool loadContent(const QVector<LogEntry>& content) override;
    void applyFilter(const FilterOptions& options) override;

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
    QString m_filterQuery;
    QMap<LogcatEntry::Level, bool> m_levelFilters;
    int m_contextLinesBefore{0};
    int m_contextLinesAfter{0};
};

#endif // LOGCATVIEWER_H
