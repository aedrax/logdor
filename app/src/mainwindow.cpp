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
#include <QtConcurrent/QtConcurrent>
#include <vector>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_pluginManager(new PluginManager(this))
    , m_filterInput(new QLineEdit(this))
    , m_caseSensitiveCheckBox(new QCheckBox(tr("Case Sensitive"), this))
    , m_beforeSpinBox(new QSpinBox(this))
    , m_afterSpinBox(new QSpinBox(this))
    , m_filterTimer(new QTimer(this))
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
    
    m_caseSensitiveCheckBox->setToolTip(tr("Toggle case sensitive filtering"));
    filterToolBar->addWidget(m_caseSensitiveCheckBox);
    
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

// Structure to hold a chunk of the file to process
struct FileChunk {
    const char* start;
    const char* end;
    bool isFirstChunk;
};

// Function to process a single chunk and return its lines
QVector<QPair<const char*, qsizetype>> processChunk(const FileChunk& chunk) {
    QVector<QPair<const char*, qsizetype>> lines;
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

    // Parse the log file into m_logEntries using QtConcurrent
    const size_t fileSize = m_currentFile.size();
    const int numThreads = QThread::idealThreadCount();
    const size_t chunkSize = fileSize / numThreads;
    
    // Create chunks to process
    QVector<FileChunk> chunks;
    const char* chunkStart = data;
    const char* chunkEnd = data + chunkSize;
    const char* fileEnd = data + fileSize;
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
    QFuture<QVector<QPair<const char*, qsizetype>>> future = 
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
        if (plugin->loadContent(m_logEntries)) {
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

void MainWindow::onFilterChanged()
{
    FilterOptions options(m_filterInput->text(),
                         m_beforeSpinBox->value(),
                         m_afterSpinBox->value(),
                         m_caseSensitiveCheckBox->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive);

    // Apply filter to all active plugins with context lines
    for (PluginInterface* plugin : m_activePlugins) {
        plugin->applyFilter(options);
    }
}
