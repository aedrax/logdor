#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QToolBar>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <thread>
#include <vector>
#include <mutex>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_pluginManager(new PluginManager(this))
    , m_filterInput(new QLineEdit(this))
    , m_beforeSpinBox(new QSpinBox(this))
    , m_afterSpinBox(new QSpinBox(this))
{
    ui->setupUi(this);
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
    
    connect(m_filterInput, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);
    connect(m_beforeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onFilterChanged);
    connect(m_afterSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onFilterChanged);

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
    m_currentFile.setFileName(fileName);
    
    if (!m_currentFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not open file: %1").arg(fileName));
        return false;
    }

    // Map the entire file into memory
    auto *data = reinterpret_cast<const char*>(m_currentFile.map(0, m_currentFile.size()));
    if (!data) {
        qWarning() << tr("Failed to map file:") << m_currentFile.errorString();
        return false;
    }

    qDebug() << tr("File mapped successfully");

    // Parse the log file into m_logEntries using parallel processing
    const size_t fileSize = m_currentFile.size();
    const unsigned int numThreads = std::thread::hardware_concurrency();
    const size_t chunkSize = fileSize / numThreads;
    
    struct ChunkResult {
        std::vector<std::pair<const char*, const char*>> lines;  // start and end of each line
        const char* chunkStart;  // where this chunk started
    };
    
    std::vector<ChunkResult> results(numThreads);
    std::vector<std::thread> threads;
    
    // Process each chunk in parallel
    for (unsigned int i = 0; i < numThreads; ++i) {
        const char* chunkStart = data + (i * chunkSize);
        const char* chunkEnd = (i == numThreads - 1) ? data + fileSize : data + ((i + 1) * chunkSize);
        
        // If not the first chunk, move start to next line boundary
        if (i > 0) {
            while (chunkStart < chunkEnd && *(chunkStart - 1) != '\n') {
                ++chunkStart;
            }
        }
        
        threads.emplace_back([chunkStart, chunkEnd, i, &results]() {
            ChunkResult& result = results[i];
            result.chunkStart = chunkStart;
            
            const char* current = chunkStart;
            while (current < chunkEnd) {
                auto next = reinterpret_cast<const char*>(memchr(current, '\n', chunkEnd - current));
                if (!next) {
                    next = chunkEnd;
                }
                result.lines.emplace_back(current, next);
                current = next + 1;
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Combine results in order
    m_logEntries.clear();
    for (const auto& result : results) {
        for (const auto& line : result.lines) {
            m_logEntries.append(LogEntry(line.first, line.second - line.first));
        }
    }
    qDebug() << tr("File parsed successfully");
    
    bool success = false;

    // Try to load the content with each plugin
    for (PluginInterface* plugin : m_activePlugins) {
        if (plugin->loadContent(m_logEntries)) {
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

void MainWindow::onFilterChanged()
{
    FilterOptions options(m_filterInput->text(),
                         m_beforeSpinBox->value(),
                         m_afterSpinBox->value());

    // Apply filter to all active plugins with context lines
    for (PluginInterface* plugin : m_activePlugins) {
        plugin->applyFilter(options);
    }
}
