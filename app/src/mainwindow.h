#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFile>
#include <QMainWindow>
#include <QTimer>
#include <QCheckBox>
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
    QFile m_currentFile;
    QList<LogEntry> m_logEntries;
    
    // Filter controls
    QLineEdit* m_filterInput;
    QCheckBox* m_caseSensitiveCheckBox;
    QSpinBox* m_beforeSpinBox;
    QSpinBox* m_afterSpinBox;
    QTimer* m_filterTimer;
};

#endif // MAINWINDOW_H
