#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_pluginManager(new PluginManager(this))
{
    ui->setupUi(this);
    // this allows the dock widget to use the full window
    this->setCentralWidget(nullptr);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onActionOpenTriggered);

    loadPlugins();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadPlugins()
{
    m_pluginManager->loadPlugins();

    // Create dock widgets for each plugin
    for (PluginInterface* plugin : m_pluginManager->plugins()) {
        QDockWidget* dock = new QDockWidget(plugin->name(), this);
        dock->setWidget(plugin->widget());
        addDockWidget(Qt::LeftDockWidgetArea, dock);
        m_activePlugins[plugin->name()] = plugin;
    }
}

bool MainWindow::openFile(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not open file: %1").arg(fileName));
        return false;
    }

    QByteArray content = file.readAll();
    file.close();

    bool success = false;

    // Try to load the content with each plugin
    for (PluginInterface* plugin : m_activePlugins) {
        if (plugin->loadContent(content)) {
            success = true;
        }
    }

    if (!success) {
        QMessageBox::warning(this, tr("Error"),
            tr("No plugin was able to load the file: %1").arg(fileName));
    }

    return success;
}

void MainWindow::onActionOpenTriggered()
{
    QFileDialog fileDialog(this, tr("Open File"), QString(), tr("All Files (*)"));
    while (fileDialog.exec() == QDialog::Accepted
        && !openFile(fileDialog.selectedFiles().constFirst())) {
    }
}
