#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "pluginmanager.h"
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

public slots:
    bool openFile(const QString& fileName);

private slots:
    void onActionOpenTriggered();

private:
    Ui::MainWindow* ui;
    PluginManager* m_pluginManager;
    QMap<QString, PluginInterface*> m_activePlugins;

    void loadPlugins();
};
#endif // MAINWINDOW_H
