#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFile>
#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include <QSettings>
#include "pluginmanager.h"

#define FILTER_DEBOUNCE_TIMEOUT_MILLISECONDS 300

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QLineEdit;
class QSpinBox;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onActionOpenTriggered();
    void onFilterChanged();
    void onFocusFilterInput();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void loadPlugins();
    bool openFile(const QString& fileName);
    void saveSettings();
    void loadSettings();

    Ui::MainWindow* ui;
    PluginManager* m_pluginManager;
    QMap<QString, PluginInterface*> m_activePlugins;
    QMap<QString, QDockWidget*> m_pluginDocks;
    QMap<QString, QAction*> m_pluginActions;
    QMenu* m_pluginsMenu;
    QFile m_currentFile;
    const char* m_mappedFile;  // Track the mapped memory
    QList<LogEntry> m_logEntries;
    FilterOptions m_filterOptions;
    
    // Filter controls
    QLineEdit* m_filterInput;
    QPushButton* m_caseSensitiveButton;
    QPushButton* m_invertFilterButton;
    QPushButton* m_queryModeButton;
    QPushButton* m_regexModeButton;
    QSpinBox* m_beforeSpinBox;
    QSpinBox* m_afterSpinBox;
    QTimer* m_filterTimer;
};

#endif // MAINWINDOW_H
