#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "backgroundtaskmanager.h"
#include "legacybridge.h"
#include "progressdialog.h"
#include "pluginprocessingtask.h"
#include "annotationexporter.h"
#include <QLabel>
#include <QSaveFile>
#include <QStandardPaths>
#include <logdor/Query.h>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QToolBar>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QShortcut>
#include <QRegularExpression>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_pluginManager(new PluginManager(this))
    , m_filterInput(new QLineEdit(this))
    , m_caseSensitiveButton(new QPushButton(tr("Aa"), this))
    , m_invertFilterButton(new QPushButton(tr("!"), this))
    , m_queryModeButton(new QPushButton(tr("Q"), this))
    , m_regexModeButton(new QPushButton(tr(".*"), this))
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
    
    // Configure toggle buttons
    auto setupToggleButton = [](QPushButton* button, const QString& tooltip) {
        button->setCheckable(true);
        button->setFlat(false);  // Make buttons non-flat for more obvious appearance
        button->setFixedSize(32, 24);  // Slightly larger
        button->setToolTip(tooltip);
        button->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #777777;"  // Visible border
            "  padding: 2px;"
            "  border-radius: 3px;"
            "}"
            "QPushButton:checked {"
            "  background-color: #0e639c;"   // VSCode blue when checked
            "  border: 1px solid #1177bb;"   // Brighter border when checked
            "  font-weight: bold;"           // Bold text when checked
            "}"
            "QPushButton:hover:!checked {"
            "  background-color: #3e3e3e;"   // Lighter background on hover
            "  border: 1px solid #999999;"   // Lighter border on hover
            "}"
        );
    };
    
    setupToggleButton(m_caseSensitiveButton, tr("Toggle case sensitive filtering"));
    filterToolBar->addWidget(m_caseSensitiveButton);
    
    setupToggleButton(m_invertFilterButton, tr("Show lines that don't match the filter"));
    filterToolBar->addWidget(m_invertFilterButton);
    
    setupToggleButton(m_regexModeButton, tr("Treat filter as a regular expression"));
    filterToolBar->addWidget(m_regexModeButton);
    
    setupToggleButton(m_queryModeButton,
                      tr("Field query mode: level:error tag:Wifi* pid>=100 \"free text\""));
    filterToolBar->addWidget(m_queryModeButton);

    // Query mode and regex mode are mutually exclusive filter languages.
    connect(m_queryModeButton, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            m_regexModeButton->setChecked(false);
    });
    connect(m_regexModeButton, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            m_queryModeButton->setChecked(false);
    });
    
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
    connect(m_caseSensitiveButton, &QPushButton::toggled, this, &MainWindow::onFilterChanged);
    connect(m_invertFilterButton, &QPushButton::toggled, this, &MainWindow::onFilterChanged);
    connect(m_regexModeButton, &QPushButton::toggled, this, &MainWindow::onFilterChanged);
    connect(m_queryModeButton, &QPushButton::toggled, this, &MainWindow::onFilterChanged);

    // Setup Ctrl+L shortcut to focus filter input
    QShortcut* filterShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(filterShortcut, &QShortcut::activated, this, &MainWindow::onFocusFilterInput);

    // Indexing runs on a worker thread; the watcher delivers progress and
    // completion back on the GUI thread.
    m_indexWatcher = new QFutureWatcher<logdor::IndexingResult>(this);
    connect(m_indexWatcher, &QFutureWatcherBase::progressValueChanged,
            this, &MainWindow::onIndexingProgress);
    connect(m_indexWatcher, &QFutureWatcherBase::finished,
            this, &MainWindow::onIndexingFinished);

    // Annotations: one hub for all viewers, autosaved on a short debounce so
    // notes are never lost silently.
    m_annotationHub = new AnnotationHub(this);
    {
        QSettings settings("Logdor", "Logdor");
        const QString author =
            settings.value("annotations/author").toString();
        if (!author.isEmpty())
            m_annotationHub->setAuthor(author);
    }
    m_annotationSaveTimer = new QTimer(this);
    m_annotationSaveTimer->setSingleShot(true);
    m_annotationSaveTimer->setInterval(1000);
    connect(m_annotationSaveTimer, &QTimer::timeout,
            this, &MainWindow::flushAnnotationSave);
    connect(m_annotationHub, &AnnotationHub::annotationsChanged, this, [this]() {
        if (m_annotationHub->isDirty())
            m_annotationSaveTimer->start();
        updateNoteCount();
    });
    connect(m_annotationHub, &AnnotationHub::reanchorFinished, this,
            [this](int, int reanchored, int orphaned) {
                if (reanchored > 0 || orphaned > 0)
                    ui->statusbar->showMessage(
                        tr("Annotations: %1 re-anchored, %2 orphaned")
                            .arg(reanchored)
                            .arg(orphaned),
                        5000);
                updateNoteCount();
            });
    m_noteCountLabel = new QLabel(this);
    ui->statusbar->addPermanentWidget(m_noteCountLabel);
    m_noteCountLabel->hide();

    QAction* importAction = ui->menuFile->addAction(tr("Import Annotations..."));
    connect(importAction, &QAction::triggered, this, &MainWindow::importAnnotations);
    QAction* exportAction = ui->menuFile->addAction(tr("Export Annotations..."));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportAnnotations);

    loadPlugins();
    loadSettings();
    initializeBackgroundProcessing();
}

// Background processing threshold: 5MB
const qint64 MainWindow::BACKGROUND_PROCESSING_THRESHOLD = 5 * 1024 * 1024;

MainWindow::~MainWindow()
{
    // The indexing task holds its own shared_ptr to the file source, so it
    // can finish (or notice the cancel) safely after we're gone.
    m_indexWatcher->cancel();
    shutdownBackgroundProcessing();
    delete ui;
}

void MainWindow::loadPlugins()
{
    m_pluginManager->loadPlugins();
    m_pluginManager->setAnnotationHub(m_annotationHub);

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
        
        // Connect action toggle to plugin state and visibility
        connect(action, &QAction::toggled, [this, dock, plugin](bool checked) {
            dock->setVisible(checked);
            plugin->setEnabled(checked);
            // Process the open file when a plugin is enabled late
            if (checked && m_fileSource && m_lineIndex) {
                if (plugin->wantsCoreSource()) {
                    plugin->setCoreSource(m_fileSource, m_lineIndex);
                } else {
                    // Enabling a legacy plugin after a core-only open pays
                    // the legacy materialization cost now, by choice.
                    QString error;
                    if (!ensureLegacyEntries(&error)) {
                        ui->statusbar->showMessage(error, 5000);
                        return;
                    }
                    plugin->setLogs(m_logEntries);
                }
                plugin->setFilter(m_filterOptions);
            }
        });
        
        // Connect dock visibility to just update action state
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
    
    // Check if any plugin settings exist
    settings.beginGroup("Plugins");
    bool hasPluginSettings = !settings.childGroups().isEmpty() || !settings.childKeys().isEmpty();
    
    // Restore plugin visibility
    for (auto it = m_pluginActions.begin(); it != m_pluginActions.end(); ++it) {
        if (hasPluginSettings) {
            // Use saved settings if they exist
            // "Annotations" replaced "Bookmark Viewer"; honor the old key
            // once so upgrading users keep their panel visibility.
            const QVariant fallback = it.key() == QLatin1String("Annotations")
                ? settings.value("Bookmark Viewer/visible", false)
                : QVariant(false);
            bool visible = settings.value(it.key() + "/visible", fallback).toBool();
            it.value()->setChecked(visible);
        } else {
            // If no settings exist, only enable plaintextviewer and selectedlineviewer by default
            bool isDefaultEnabled = (it.key() == "Plain Text Viewer" || it.key() == "Selected Line Viewer");
            it.value()->setChecked(isDefaultEnabled);
        }
    }
    settings.endGroup();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_indexWatcher->cancel();
    flushAnnotationSave();
    saveSettings();
    QMainWindow::closeEvent(event);
}

bool MainWindow::openFile(const QString& fileName)
{
    // Stop any in-flight indexing. setFuture() below detaches the watcher
    // from the old future, so no stale finished() arrives; the cancelled
    // task keeps the old source alive through its own shared_ptr.
    if (m_indexWatcher->isRunning()) {
        m_indexWatcher->cancel();
    }

    // Save the outgoing file's notes before anything else can fail.
    flushAnnotationSave();
    m_annotationHub->clear();
    m_currentFileName.clear();
    updateNoteCount();

    // Clear plugins' data before dropping the previous mapping — they hold
    // pointers (legacy) or shared_ptrs (core) into it.
    m_logEntries.clear();
    for (PluginInterface* plugin : m_activePlugins) {
        plugin->setLogs(m_logEntries);
    }
    m_pluginManager->setCoreSource(nullptr, nullptr);
    m_lineIndex.reset();
    m_fileSource.reset();

    QString error;
    auto source = logdor::FileSource::open(fileName, &error);
    if (!source) {
        QMessageBox::warning(this, tr("Error"), error);
        return false;
    }
    if (source->mode() == logdor::FileSource::Mode::Buffered) {
        qWarning() << "Memory mapping failed for" << fileName
                   << "- using buffered reads";
    }

    m_fileSource = std::move(source);
    m_pendingFileName = fileName;
    setWindowTitle(tr("Logdor - %1 (Indexing...)").arg(QFileInfo(fileName).fileName()));

    // Reuse the shared progress dialog for the indexing stage; the plugin
    // processing stage (BackgroundTaskManager) takes it over afterwards.
    if (m_progressDialog) {
        m_progressDialog->setTitle(tr("Indexing %1").arg(QFileInfo(fileName).fileName()));
        m_progressDialog->setDescription(tr("Scanning line boundaries..."));
        m_progressDialog->setCancelable(true);
        m_progressDialog->setAutoClose(false);
        m_progressDialog->setMinimumDuration(1000);
        m_progressDialog->showProgress();
    }

    m_indexWatcher->setFuture(logdor::buildLineIndex(m_fileSource));

    // true means "open + indexing started"; failures after this point are
    // reported asynchronously from onIndexingFinished().
    return true;
}

void MainWindow::onIndexingProgress(int permille)
{
    if (!m_progressDialog || m_pendingFileName.isEmpty()) {
        return;
    }
    ProgressInfo info;
    info.percentage = permille / 10;
    info.processedItems = permille;
    info.totalItems = 1000;
    info.statusMessage = tr("Scanning line boundaries...");
    m_progressDialog->updateProgress(info);
}

void MainWindow::onIndexingFinished()
{
    if (m_progressDialog) {
        m_progressDialog->hideProgress();
    }

    const QString fileName = m_pendingFileName;
    m_pendingFileName.clear();

    if (m_indexWatcher->future().isCanceled()) {
        setWindowTitle(tr("Logdor"));
        ui->statusbar->showMessage(tr("Indexing cancelled"), 3000);
        return;
    }

    const logdor::IndexingResult result = m_indexWatcher->future().result();
    m_lineIndex = result.index;

    qDebug() << "Indexed" << result.lineCount << "lines in" << result.elapsedMs
             << "ms," << (m_fileSource->mode() == logdor::FileSource::Mode::Mapped
                              ? "mapped" : "buffered");
    ui->statusbar->showMessage(tr("Indexed %L1 lines in %2 ms")
                                   .arg(result.lineCount)
                                   .arg(result.elapsedMs),
                               3000);

    // Annotations first, so viewers paint markers on their first fill.
    m_currentFileName = fileName;
    m_annotationHub->beginFile(m_fileSource, m_lineIndex,
                               loadAnnotationSidecars());
    updateNoteCount();

    // Core-source plugins go live immediately, on the GUI thread — no
    // materialized entry list, no background stage.
    m_pluginManager->setCoreSource(m_fileSource, m_lineIndex);

    if (!m_pluginManager->anyEnabledLegacyPlugin()) {
        // Index-only path: for a 5 GB file this is ~200 MB of RAM total.
        m_pluginManager->setFilter(m_filterOptions);
        setWindowTitle(tr("Logdor - %1").arg(QFileInfo(fileName).fileName()));
        return;
    }

    // At least one legacy plugin needs the QList<LogEntry> view of the file.
    QString error;
    if (!ensureLegacyEntries(&error)) {
        QMessageBox::warning(this, tr("Error"), error);
        setWindowTitle(tr("Logdor"));
        return;
    }

    if (shouldUseBackgroundProcessing(fileName)) {
        processFileInBackground(fileName, m_logEntries);
        // Core plugins shouldn't wait for the legacy stage to filter.
        m_pluginManager->setFilter(m_filterOptions);
        setWindowTitle(tr("Logdor - %1 (Loading...)").arg(QFileInfo(fileName).fileName()));
    } else {
        const bool success = m_pluginManager->setLogs(m_logEntries, fileName);
        m_pluginManager->setFilter(m_filterOptions);

        if (!success) {
            QMessageBox::warning(this, tr("Error"),
                tr("No plugin was able to load the file: %1").arg(fileName));
            setWindowTitle(tr("Logdor"));
        } else {
            setWindowTitle(tr("Logdor - %1").arg(QFileInfo(fileName).fileName()));
        }
    }
}

QString MainWindow::annotationSidecarPath() const
{
    return m_currentFileName.isEmpty()
        ? QString()
        : m_currentFileName + QStringLiteral(".logdor.json");
}

QString MainWindow::annotationFallbackPath(const logdor::FileIdentity& identity) const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/annotations/")
        + QString::fromUtf8(identity.prefixSha256)
        + QStringLiteral(".logdor.json");
}

logdor::AnnotationSet MainWindow::loadAnnotationSidecars()
{
    // Identity gates auto-load: a sidecar written for different content is
    // ignored (Import remains available for deliberate merges).
    const logdor::FileIdentity identity =
        logdor::computeFileIdentity(*m_fileSource);

    logdor::AnnotationSet merged;
    bool first = true;
    for (const QString& path :
         { annotationSidecarPath(), annotationFallbackPath(identity) }) {
        QFile file(path);
        if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
            continue;
        logdor::AnnotationFileError error;
        const auto loaded = logdor::loadAnnotations(file.readAll(), &error);
        if (!loaded) {
            ui->statusbar->showMessage(
                tr("Could not read annotations from %1: %2").arg(path, error.message),
                8000);
            continue;
        }
        if (logdor::matchIdentity(loaded->identity, *m_fileSource)
            == logdor::IdentityMatch::Mismatch) {
            ui->statusbar->showMessage(
                tr("Ignoring %1: it belongs to a different file").arg(path), 8000);
            continue;
        }
        for (const QString& warning : loaded->warnings)
            qWarning() << "annotation sidecar" << path << ":" << warning;
        merged = first ? loaded->set
                       : logdor::mergeAnnotations(merged, loaded->set);
        first = false;
    }
    merged.clearDirty(); // loading isn't an edit
    return merged;
}

void MainWindow::flushAnnotationSave()
{
    if (!m_annotationHub->hasFile() || !m_annotationHub->isDirty())
        return;

    const QByteArray bytes = logdor::saveAnnotations(
        m_annotationHub->set(), m_annotationHub->identity(),
        QFileInfo(m_currentFileName).fileName());

    const auto writeTo = [&bytes](const QString& path) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(bytes);
        return file.commit();
    };

    if (writeTo(annotationSidecarPath())) {
        m_annotationHub->clearDirty();
        return;
    }
    // The log's directory isn't writable: keep notes in the app data dir,
    // keyed by content so they reunite with the log wherever it goes.
    const QString fallback = annotationFallbackPath(m_annotationHub->identity());
    QDir().mkpath(QFileInfo(fallback).absolutePath());
    if (writeTo(fallback)) {
        m_annotationHub->clearDirty();
        ui->statusbar->showMessage(
            tr("Log directory is not writable; notes saved to %1").arg(fallback),
            5000);
    } else {
        ui->statusbar->showMessage(
            tr("FAILED to save annotations — check disk space and permissions"),
            10000);
    }
}

void MainWindow::updateNoteCount()
{
    const auto count = m_annotationHub->set().size();
    if (count == 0) {
        m_noteCountLabel->hide();
        return;
    }
    int orphaned = 0;
    for (const auto& annotation : m_annotationHub->set().annotations())
        orphaned += annotation.orphaned ? 1 : 0;
    m_noteCountLabel->setText(orphaned > 0
        ? tr("%1 notes (%2 orphaned)").arg(count).arg(orphaned)
        : tr("%1 notes").arg(count));
    m_noteCountLabel->show();
}

void MainWindow::importAnnotations()
{
    if (!m_annotationHub->hasFile()) {
        QMessageBox::information(this, tr("Import Annotations"),
                                 tr("Open a log file first."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Annotations"), QString(),
        tr("Logdor annotations (*.logdor.json);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import Annotations"),
                             tr("Cannot open %1").arg(path));
        return;
    }
    logdor::AnnotationFileError error;
    const auto loaded = logdor::loadAnnotations(file.readAll(), &error);
    if (!loaded) {
        QMessageBox::warning(this, tr("Import Annotations"), error.message);
        return;
    }
    if (logdor::matchIdentity(loaded->identity, *m_fileSource)
            == logdor::IdentityMatch::Mismatch
        && QMessageBox::question(
               this, tr("Import Annotations"),
               tr("These annotations were made on a different file. Line "
                  "positions will be re-anchored by content where possible.\n\n"
                  "Import anyway?"))
            != QMessageBox::Yes) {
        return;
    }
    m_annotationHub->mergeFrom(loaded->set);
    ui->statusbar->showMessage(
        tr("Imported %1 annotations from %2").arg(loaded->set.size()).arg(path),
        5000);
}

void MainWindow::exportAnnotations()
{
    if (m_annotationHub->set().isEmpty()) {
        QMessageBox::information(this, tr("Export Annotations"),
                                 tr("There are no annotations to export."));
        return;
    }
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Annotations"), QString(),
        tr("HTML report (*.html);;CSV (*.csv)"), &selectedFilter);
    if (path.isEmpty())
        return;

    const QByteArray bytes = selectedFilter.contains(QLatin1String("CSV"))
        ? exportAnnotationsCsv(m_annotationHub->set())
        : exportAnnotationsHtml(
              m_annotationHub->set(),
              [this](qint64 line) { return m_annotationHub->lineText(line); },
              QFileInfo(m_currentFileName).fileName());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || (file.write(bytes), !file.commit())) {
        QMessageBox::warning(this, tr("Export Annotations"),
                             tr("Could not write %1").arg(path));
        return;
    }
    ui->statusbar->showMessage(tr("Exported annotations to %1").arg(path), 5000);
}

bool MainWindow::ensureLegacyEntries(QString* error)
{
    if (!m_fileSource || !m_lineIndex) {
        if (error)
            *error = tr("No file is open");
        return false;
    }
    if (!m_logEntries.isEmpty() || m_lineIndex->lineCount() == 0)
        return true;

    // Legacy plugins consume pointers into one contiguous range. In buffered
    // mode (mmap failed) that means heap-loading the whole file once.
    if (!m_fileSource->isContiguous() && !m_fileSource->ensureContiguous(error))
        return false;

    m_logEntries = materializeLegacyEntries(*m_fileSource, *m_lineIndex);
    qDebug() << "Materialized legacy entry list:" << m_logEntries.size() << "entries";
    return true;
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
    // Tint the input by validity: regex mode validates the pattern, query
    // mode validates syntax only (schema-aware errors surface per viewer).
    if (m_regexModeButton->isChecked()) {
        QRegularExpression regex(m_filterInput->text());
        if (regex.isValid() || m_filterInput->text().isEmpty()) {
            m_filterInput->setStyleSheet("QLineEdit { background-color: #90EE90; color: black; }"); // Light green
        } else {
            m_filterInput->setStyleSheet("QLineEdit { background-color: #FFB6C1; color: black; }"); // Light red
        }
    } else if (m_queryModeButton->isChecked() && !m_filterInput->text().isEmpty()) {
        logdor::QueryError error;
        const auto query = logdor::CompiledQuery::compile(
            m_filterInput->text(), {},
            m_caseSensitiveButton->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive,
            logdor::QueryOption::AllowUnknownFields, &error);
        if (query) {
            m_filterInput->setStyleSheet("QLineEdit { background-color: #90EE90; color: black; }");
            m_filterInput->setToolTip(QString());
        } else {
            m_filterInput->setStyleSheet("QLineEdit { background-color: #FFB6C1; color: black; }");
            m_filterInput->setToolTip(error.message);
        }
    } else {
        m_filterInput->setStyleSheet("");
        m_filterInput->setToolTip(QString());
    }

    FilterOptions options(m_filterInput->text(),
                         m_beforeSpinBox->value(),
                         m_afterSpinBox->value(),
                         m_caseSensitiveButton->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive,
                         m_invertFilterButton->isChecked(),
                         m_queryModeButton->isChecked(),
                         m_regexModeButton->isChecked());

    m_filterOptions = options;

    // Apply filter to all enabled plugins with context lines
    m_pluginManager->setFilter(m_filterOptions);
}
// Background processing methods
void MainWindow::initializeBackgroundProcessing()
{
    // Initialize background task manager
    m_backgroundTaskManager = std::make_unique<BackgroundTaskManager>(this);
    
    // Connect background task manager signals
    connect(m_backgroundTaskManager.get(), &BackgroundTaskManager::taskStarted,
            this, &MainWindow::onBackgroundTaskStarted);
    connect(m_backgroundTaskManager.get(), &BackgroundTaskManager::taskCompleted,
            this, &MainWindow::onBackgroundTaskCompleted);
    connect(m_backgroundTaskManager.get(), &BackgroundTaskManager::taskCancelled,
            this, &MainWindow::onBackgroundTaskCancelled);
    connect(m_backgroundTaskManager.get(), &BackgroundTaskManager::taskFailed,
            this, &MainWindow::onBackgroundTaskFailed);
    connect(m_backgroundTaskManager.get(), &BackgroundTaskManager::taskProgressChanged,
            this, &MainWindow::onBackgroundTaskProgressChanged);
    
    // Initialize progress dialog
    m_progressDialog = std::make_unique<ProgressDialog>(this);
    connect(m_progressDialog.get(), &ProgressDialog::cancelled,
            this, &MainWindow::onProgressDialogCancelled);
    
    // Initialize status bar progress
    m_statusBarProgress = std::make_unique<StatusBarProgress>(this);
    connect(m_statusBarProgress.get(), &StatusBarProgress::clicked,
            this, &MainWindow::onStatusBarProgressClicked);
    
    // Add status bar progress to status bar
    ui->statusbar->addPermanentWidget(m_statusBarProgress.get());
    m_statusBarProgress->hide(); // Initially hidden
    
    qDebug() << "Background processing initialized";
}

void MainWindow::shutdownBackgroundProcessing()
{
    if (m_backgroundTaskManager) {
        qDebug() << "Shutting down background processing";
        
        // Cancel any running tasks
        if (!m_currentBackgroundTaskId.isEmpty()) {
            m_backgroundTaskManager->cancelTask(m_currentBackgroundTaskId);
        }
        
        // Shutdown the task manager
        m_backgroundTaskManager->shutdown();
        m_backgroundTaskManager.reset();
    }
    
    // Clean up UI components
    if (m_progressDialog) {
        m_progressDialog->hide();
        m_progressDialog.reset();
    }
    
    if (m_statusBarProgress) {
        m_statusBarProgress->hide();
        m_statusBarProgress.reset();
    }
    
    m_currentBackgroundTaskId.clear();
}

void MainWindow::processFileInBackground(const QString& fileName, const QList<LogEntry>& logEntries)
{
    if (!m_backgroundTaskManager) {
        qWarning() << "Background task manager not initialized";
        return;
    }
    
    // Cancel any existing background task
    if (!m_currentBackgroundTaskId.isEmpty()) {
        m_backgroundTaskManager->cancelTask(m_currentBackgroundTaskId);
        m_currentBackgroundTaskId.clear();
    }
    
    // Create plugin processing task
    auto task = PluginProcessingTaskFactory::createTask(fileName, logEntries, m_pluginManager, TaskPriority::High);
    if (!task) {
        qWarning() << "Failed to create plugin processing task";
        return;
    }
    
    // Submit task to background manager
    QString taskId = m_backgroundTaskManager->submitTask(task);
    if (taskId.isEmpty()) {
        qWarning() << "Failed to submit plugin processing task";
        return;
    }
    
    m_currentBackgroundTaskId = taskId;
    
    // Show progress indication
    showProgressDialog(taskId, QString("Loading %1").arg(QFileInfo(fileName).fileName()));
    updateStatusBarProgress(taskId);
    
    qDebug() << "Started background processing for file:" << fileName << "with task ID:" << taskId;
}

void MainWindow::showProgressDialog(const QString& taskId, const QString& title)
{
    if (!m_progressDialog || taskId.isEmpty()) {
        return;
    }
    
    m_progressDialog->setTitle(title);
    m_progressDialog->setDescription("Processing log file with plugins...");
    m_progressDialog->setCancelable(true);
    m_progressDialog->setAutoClose(false);
    m_progressDialog->setMinimumDuration(1000); // Show after 1 second
    
    // The progress dialog will be connected to the task's progress tracker
    // This happens in the background task manager when progress callbacks are set
    
    m_progressDialog->showProgress();
}

void MainWindow::hideProgressDialog()
{
    if (m_progressDialog) {
        m_progressDialog->hideProgress();
    }
}

void MainWindow::updateStatusBarProgress(const QString& taskId)
{
    if (!m_statusBarProgress || taskId.isEmpty()) {
        return;
    }
    
    // The status bar progress will be updated through the task progress callbacks
    m_statusBarProgress->startProgress();
    m_statusBarProgress->show();
}

bool MainWindow::shouldUseBackgroundProcessing(const QString& fileName) const
{
    QFileInfo fileInfo(fileName);
    if (!fileInfo.exists()) {
        return false;
    }
    
    // Use background processing for files larger than the threshold
    return fileInfo.size() > BACKGROUND_PROCESSING_THRESHOLD;
}

// Background processing event handlers
void MainWindow::onBackgroundTaskStarted(const QString& taskId)
{
    if (taskId == m_currentBackgroundTaskId) {
        qDebug() << "Background task started:" << taskId;
        
        // Set up progress callbacks for the task
        if (m_backgroundTaskManager) {
            // Connect progress dialog to task progress
            if (m_progressDialog) {
                m_backgroundTaskManager->setProgressCallback(taskId, 
                    [this](const ProgressInfo& progress) {
                        if (m_progressDialog) {
                            m_progressDialog->updateProgress(progress);
                        }
                    });
                
                m_backgroundTaskManager->setStatusCallback(taskId,
                    [this](const QString& status) {
                        if (m_progressDialog) {
                            m_progressDialog->updateStatus(status);
                        }
                    });
            }
            
            // Connect status bar progress to task progress
            if (m_statusBarProgress) {
                m_backgroundTaskManager->setProgressCallback(taskId,
                    [this](const ProgressInfo& progress) {
                        if (m_statusBarProgress) {
                            m_statusBarProgress->updateProgress(progress);
                        }
                    });
                
                m_backgroundTaskManager->setStatusCallback(taskId,
                    [this](const QString& status) {
                        if (m_statusBarProgress) {
                            m_statusBarProgress->updateStatus(status);
                        }
                    });
            }
        }
    }
}

void MainWindow::onBackgroundTaskCompleted(const QString& taskId, const TaskResult& result)
{
    if (taskId == m_currentBackgroundTaskId) {
        qDebug() << "Background task completed:" << taskId;
        
        m_currentBackgroundTaskId.clear();
        
        // Hide progress indicators
        hideProgressDialog();
        if (m_statusBarProgress) {
            m_statusBarProgress->stopProgress();
        }
        
        // Check if the task was successful
        if (result.isSuccess()) {
            // Apply filters to the loaded data
            m_pluginManager->setFilter(m_filterOptions);
            
            // Update window title to remove "Loading..." indicator
            QString currentTitle = windowTitle();
            if (currentTitle.contains(" (Loading...)")) {
                setWindowTitle(currentTitle.replace(" (Loading...)", ""));
            }
            
            // Show completion message in status bar
            ui->statusbar->showMessage("File loaded successfully", 3000);
            
            qDebug() << "Plugin processing completed successfully";
        } else {
            // Handle processing failure
            QString errorMsg = result.errorMessage.isEmpty() ? 
                "Unknown error occurred during processing" : result.errorMessage;
            
            QMessageBox::warning(this, tr("Processing Error"),
                tr("Failed to process file with plugins:\n%1").arg(errorMsg));
            
            qWarning() << "Plugin processing failed:" << errorMsg;
        }
    }
}

void MainWindow::onBackgroundTaskCancelled(const QString& taskId)
{
    if (taskId == m_currentBackgroundTaskId) {
        qDebug() << "Background task cancelled:" << taskId;
        
        m_currentBackgroundTaskId.clear();
        
        // Hide progress indicators
        hideProgressDialog();
        if (m_statusBarProgress) {
            m_statusBarProgress->stopProgress();
        }
        
        // Update window title to remove "Loading..." indicator
        QString currentTitle = windowTitle();
        if (currentTitle.contains(" (Loading...)")) {
            setWindowTitle(currentTitle.replace(" (Loading...)", " (Cancelled)"));
        }
        
        // Show cancellation message in status bar
        ui->statusbar->showMessage("File loading cancelled", 3000);
    }
}

void MainWindow::onBackgroundTaskFailed(const QString& taskId, const QString& error)
{
    if (taskId == m_currentBackgroundTaskId) {
        qDebug() << "Background task failed:" << taskId << "Error:" << error;
        
        m_currentBackgroundTaskId.clear();
        
        // Hide progress indicators
        hideProgressDialog();
        if (m_statusBarProgress) {
            m_statusBarProgress->stopProgress();
        }
        
        // Update window title to remove "Loading..." indicator
        QString currentTitle = windowTitle();
        if (currentTitle.contains(" (Loading...)")) {
            setWindowTitle(currentTitle.replace(" (Loading...)", " (Failed)"));
        }
        
        // Show error message
        QMessageBox::critical(this, tr("Processing Error"),
            tr("Failed to process file with plugins:\n%1").arg(error));
        
        // Show error message in status bar
        ui->statusbar->showMessage("File loading failed", 5000);
    }
}

void MainWindow::onBackgroundTaskProgressChanged(const QString& taskId, const ProgressInfo& progress)
{
    if (taskId == m_currentBackgroundTaskId) {
        // Progress updates are handled through the callbacks set in onBackgroundTaskStarted
        // This slot can be used for additional progress-related UI updates if needed
        
        // Update status bar with progress information
        if (!progress.statusMessage.isEmpty()) {
            QString statusText = QString("%1 (%2%)").arg(progress.statusMessage).arg(progress.percentage);
            ui->statusbar->showMessage(statusText);
        }
    }
}

void MainWindow::onProgressDialogCancelled()
{
    if (m_indexWatcher->isRunning()) {
        qDebug() << "User cancelled indexing";
        m_indexWatcher->cancel();
        return;
    }
    if (!m_currentBackgroundTaskId.isEmpty() && m_backgroundTaskManager) {
        qDebug() << "User cancelled progress dialog, cancelling background task:" << m_currentBackgroundTaskId;
        m_backgroundTaskManager->cancelTask(m_currentBackgroundTaskId);
    }
}

void MainWindow::onStatusBarProgressClicked()
{
    // Show detailed progress dialog when status bar progress is clicked
    if (!m_currentBackgroundTaskId.isEmpty() && m_progressDialog) {
        if (!m_progressDialog->isVisible()) {
            m_progressDialog->show();
        } else {
            m_progressDialog->raise();
            m_progressDialog->activateWindow();
        }
    }
}