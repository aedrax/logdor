#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QFile>
#include <QMainWindow>
#include <QTimer>
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

private:
    void loadPlugins();
    bool openFile(const QString& fileName);

    Ui::MainWindow* ui;
    PluginManager* m_pluginManager;
    QMap<QString, PluginInterface*> m_activePlugins;
    QFile m_currentFile;
    QVector<LogEntry> m_logEntries;
    
    // Filter controls
    QLineEdit* m_filterInput;
    QSpinBox* m_beforeSpinBox;
    QSpinBox* m_afterSpinBox;
    QTimer* m_filterTimer;
};

#endif // MAINWINDOW_H
