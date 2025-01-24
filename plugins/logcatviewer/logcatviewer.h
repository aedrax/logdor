#ifndef LOGCATVIEWER_H
#define LOGCATVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtPlugin>
#include <QAction>
#include <QMap>

class LogEntry {
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
            case Level::Verbose: return QColor(120, 120, 120);  // Gray
            case Level::Debug: return QColor(0, 120, 0);        // Dark Green
            case Level::Info: return QColor(0, 0, 0);          // Black
            case Level::Warning: return QColor(180, 130, 0);    // Orange
            case Level::Error: return QColor(200, 0, 0);       // Red
            case Level::Fatal: return QColor(160, 0, 160);     // Purple
            default: return QColor(0, 0, 0);
        }
    }
};

class LogcatViewer : public QObject, public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    LogcatViewer();
    ~LogcatViewer();

    // PluginInterface implementation
    QString name() const override { return "Logcat Viewer"; }
    QWidget* widget() override { return m_container; }
    bool loadContent(const QByteArray& content) override;
    void applyFilter(const QString& query, int contextLinesBefore = 0, int contextLinesAfter = 0) override;

private slots:
    void toggleLevel(LogEntry::Level level, bool enabled);
    void updateVisibleRows();
    void handleSort(int column, Qt::SortOrder order);

private:
    void setupUi();
    void parseLogLine(const QString& line);
    bool matchesFilter(const LogEntry& entry) const;
    void setSortRole(QTableWidgetItem* item, int column, int row) const;

    QWidget* m_container;
    QVBoxLayout* m_layout;
    QToolBar* m_toolbar;
    QTableWidget* m_table;
    QVector<LogEntry> m_entries;
    QString m_filterQuery;
    QMap<LogEntry::Level, bool> m_levelFilters;
    QMap<LogEntry::Level, QAction*> m_levelActions;
    
    // Context lines
    int m_contextLinesBefore{0};
    int m_contextLinesAfter{0};
    QVector<bool> m_directMatches;  // Tracks which lines directly match the filter
};

#endif // LOGCATVIEWER_H
