#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>
#include <QToolBar>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QShortcut>
#include <QtConcurrent/QtConcurrent>
#include <vector>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_pluginManager(new PluginManager(this))
    , m_filterInput(new QLineEdit(this))
    , m_caseSensitiveCheckBox(new QCheckBox(tr("Case Sensitive"), this))
    , m_invertFilterCheckBox(new QCheckBox(tr("Invert Filter"), this))
    , m_beforeSpinBox(new QSpinBox(this))
    , m_afterSpinBox(new QSpinBox(this))
    , m_filterTimer(new QTimer(this))
    , m_pluginsMenu(nullptr)
{
    ui->setupUi(this);
    m_pluginsMenu = ui->menuPlugins;
    // this allows the dock widget to use the full window
    this->setCentralWidget(nullptr);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onActionOpenTriggered);

    // Create filter toolbar
    QToolBar* filterToolBar = addToolBar(tr("Filter"));
    filterToolBar->setMovable(false);
    
    QLabel* filterLabel = new QLabel(tr("Filter:"), this);
    filterToolBar->addWidget(filterLabel);
    
    m_filterInput->setPlaceholderText(tr("Enter filter text..."));
    filterToolBar->addWidget(m_filterInput);
    
    m_caseSensitiveCheckBox->setToolTip(tr("Toggle case sensitive filtering"));
    filterToolBar->addWidget(m_caseSensitiveCheckBox);
    
    m_invertFilterCheckBox->setToolTip(tr("Show lines that don't match the filter"));
    filterToolBar->addWidget(m_invertFilterCheckBox);
    
    // Add context line controls
    filterToolBar->addSeparator();
    
    QLabel* beforeLabel = new QLabel(tr("Lines Before:"), this);
    filterToolBar->addWidget(beforeLabel);
    
    // m_beforeSpinBox->setRange(0, 10);
    m_beforeSpinBox->setValue(0);
    m_beforeSpinBox->setToolTip(tr("Number of context lines to show before matches"));
    filterToolBar->addWidget(m_beforeSpinBox);

    QLabel* afterLabel = new QLabel(tr("Lines After:"), this);
    filterToolBar->addWidget(afterLabel);
    
    // m_afterSpinBox->setRange(0, 10);
    m_afterSpinBox->setValue(0);
    m_afterSpinBox->setToolTip(tr("Number of context lines to show after matches"));
    filterToolBar->addWidget(m_afterSpinBox);
    
    // Set up filter timer with delay
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(FILTER_DEBOUNCE_TIMEOUT_MILLISECONDS);
    
    // Connect filter input to timer restart
    connect(m_filterInput, &QLineEdit::textChanged, m_filterTimer, qOverload<>(&QTimer::start));
    connect(m_filterTimer, &QTimer::timeout, this, &MainWindow::onFilterChanged);
    
    // Connect spin boxes directly since they don't need debouncing
    connect(m_beforeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onFilterChanged);
    connect(m_afterSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onFilterChanged);
    connect(m_caseSensitiveCheckBox, &QCheckBox::checkStateChanged, this, &MainWindow::onFilterChanged);
    connect(m_invertFilterCheckBox, &QCheckBox::checkStateChanged, this, &MainWindow::onFilterChanged);

    // Setup Ctrl+L shortcut to focus filter input
    QShortcut* filterShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(filterShortcut, &QShortcut::activated, this, &MainWindow::onFocusFilterInput);

    loadPlugins();
    loadSettings();
}

MainWindow::~MainWindow()
{
    if (m_mappedFile) {
        m_currentFile.unmap(const_cast<uchar*>(reinterpret_cast<const uchar*>(m_mappedFile)));
        m_currentFile.close();
    }
    delete ui;
}

void MainWindow::loadPlugins()
{
    m_pluginManager->loadPlugins();

    // Create dock widgets and menu actions for each plugin
    for (PluginInterface* plugin : m_pluginManager->plugins()) {
        QString pluginName = plugin->name();
        
        // Create dock widget
        QDockWidget* dock = new QDockWidget(pluginName, this);
        dock->setWidget(plugin->widget());
        dock->setObjectName(pluginName); // Important for state restoration
        addDockWidget(Qt::LeftDockWidgetArea, dock);
        m_activePlugins[pluginName] = plugin;
        m_pluginDocks[pluginName] = dock;
        
        // Create menu action
        QAction* action = new QAction(pluginName, this);
        action->setCheckable(true);
        action->setChecked(true); // Default to visible
        connect(action, &QAction::toggled, dock, &QWidget::setVisible);
        connect(dock, &QDockWidget::visibilityChanged, action, &QAction::setChecked);
        m_pluginsMenu->addAction(action);
        m_pluginActions[pluginName] = action;
    }
}

void MainWindow::saveSettings()
{
    QSettings settings("Logdor", "Logdor");
    
    // Save window geometry and state
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    
    // Save plugin visibility
    settings.beginGroup("Plugins");
    for (auto it = m_pluginActions.constBegin(); it != m_pluginActions.constEnd(); ++it) {
        settings.setValue(it.key() + "/visible", it.value()->isChecked());
    }
    settings.endGroup();
}

void MainWindow::loadSettings()
{
    QSettings settings("Logdor", "Logdor");
    
    // Restore window geometry and state
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("windowState")) {
        restoreState(settings.value("windowState").toByteArray());
    }
    
    // Restore plugin visibility
    settings.beginGroup("Plugins");
    for (auto it = m_pluginActions.begin(); it != m_pluginActions.end(); ++it) {
        bool visible = settings.value(it.key() + "/visible", true).toBool();
        it.value()->setChecked(visible);
    }
    settings.endGroup();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

// Structure to hold a chunk of the file to process
struct FileChunk {
    const char* start;
    const char* end;
    bool isFirstChunk;
};

// Function to process a single chunk and return its lines
QList<QPair<const char*, qsizetype>> processChunk(const FileChunk& chunk) {
    QList<QPair<const char*, qsizetype>> lines;
    const char* current = chunk.start;
    
    while (current < chunk.end) {
        auto next = reinterpret_cast<const char*>(memchr(current, '\n', chunk.end - current));
        if (!next) {
            next = chunk.end;
        }
        lines.append({current, next - current});
        current = next + 1;
    }
    
    return lines;
}

bool MainWindow::openFile(const QString& fileName)
{
    // Clear plugins' data before unmapping the current file
    m_logEntries.clear();
    for (PluginInterface* plugin : m_activePlugins) {
        plugin->setLogs(m_logEntries);
    }

    // Clean up previous file if any
    if (m_mappedFile) {
        m_currentFile.unmap(const_cast<uchar*>(reinterpret_cast<const uchar*>(m_mappedFile)));
        m_currentFile.close();
        m_mappedFile = nullptr;
    }

    m_currentFile.setFileName(fileName);
    
    if (!m_currentFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not open file: %1").arg(fileName));
        return false;
    }

    // Map the entire file into memory
    m_mappedFile = reinterpret_cast<const char*>(m_currentFile.map(0, m_currentFile.size()));
    if (!m_mappedFile) {
        qWarning() << tr("Failed to map file:") << m_currentFile.errorString();
        return false;
    }

    qDebug() << tr("File mapped successfully");

    // Parse the log file into m_logEntries using QtConcurrent
    const size_t fileSize = m_currentFile.size();
    const int numThreads = QThread::idealThreadCount();
    const size_t chunkSize = fileSize / numThreads;
    
    // Create chunks to process
    QList<FileChunk> chunks;
    const char* chunkStart = m_mappedFile;
    const char* chunkEnd = m_mappedFile + chunkSize;
    const char* fileEnd = m_mappedFile + fileSize;
    for (int i = 0; i < numThreads; ++i) {
        auto next = reinterpret_cast<const char*>(memchr(chunkEnd, '\n', fileEnd - chunkEnd));
        if (next) {
            chunkEnd = next;
        } else {
            chunkEnd = fileEnd;
        }
        chunks.append({chunkStart, chunkEnd, i == 0});
        chunkStart = chunkEnd + 1;
        chunkEnd = chunkStart + chunkSize;
        if (chunkEnd > fileEnd) {
            chunkEnd = fileEnd;
        }
        if (chunkStart >= fileEnd) {
            break;
        }
    }
    
    // Process chunks in parallel using QtConcurrent
    QFuture<QList<QPair<const char*, qsizetype>>> future = 
        QtConcurrent::mapped(chunks, processChunk);
    
    // Wait for all chunks to be processed and combine results
    future.waitForFinished();
    
    m_logEntries.clear();
    const auto results = future.results();
    for (const auto& chunkLines : results) {
        for (const auto& line : chunkLines) {
            m_logEntries.append(LogEntry(line.first, line.second));
        }
    }
    
    qDebug() << tr("File parsed successfully");
    
    bool success = false;

    // Try to load the content with each plugin
    for (PluginInterface* plugin : m_activePlugins) {
        if (plugin->setLogs(m_logEntries)) {
            success = true;
        }
    }

    if (!success) {
        QMessageBox::warning(this, tr("Error"),
            tr("No plugin was able to load the file: %1").arg(fileName));
    } else {
        // Update window title with the current file name
        setWindowTitle(tr("Logdor - %1").arg(QFileInfo(fileName).fileName()));
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

void MainWindow::onFocusFilterInput()
{
    m_filterInput->setFocus();
    m_filterInput->selectAll();
}

void MainWindow::onFilterChanged()
{
    FilterOptions options(m_filterInput->text(),
                         m_beforeSpinBox->value(),
                         m_afterSpinBox->value(),
                         m_caseSensitiveCheckBox->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive,
                         m_invertFilterCheckBox->isChecked());

    // Apply filter to all active plugins with context lines
    for (PluginInterface* plugin : m_activePlugins) {
        plugin->setFilter(options);
    }
}
