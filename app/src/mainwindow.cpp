#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "annotationexporter.h"
#include <QProgressDialog>
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

    // Indexing progress: modal-less, cancellable, only appears past 1 s.
    m_indexProgress = new QProgressDialog(this);
    m_indexProgress->setWindowModality(Qt::NonModal);
    m_indexProgress->setRange(0, 1000); // permille from buildLineIndex
    m_indexProgress->setMinimumDuration(1000);
    m_indexProgress->setAutoClose(false);
    m_indexProgress->setAutoReset(false);
    m_indexProgress->reset();
    connect(m_indexProgress, &QProgressDialog::canceled, this, [this]() {
        m_indexWatcher->cancel();
    });

    loadPlugins();
    loadSettings();
}

MainWindow::~MainWindow()
{
    // The indexing task holds its own shared_ptr to the file source, so it
    // can finish (or notice the cancel) safely after we're gone.
    m_indexWatcher->cancel();
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
            // Feed the open file to a plugin enabled late
            if (checked && m_fileSource && m_lineIndex) {
                plugin->setCoreSource(m_fileSource, m_lineIndex);
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
    // shared_ptrs into it.
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

    m_indexProgress->setLabelText(
        tr("Indexing %1...").arg(QFileInfo(fileName).fileName()));
    m_indexProgress->setValue(0);

    m_indexWatcher->setFuture(logdor::buildLineIndex(m_fileSource));

    // true means "open + indexing started"; failures after this point are
    // reported asynchronously from onIndexingFinished().
    return true;
}

void MainWindow::onIndexingProgress(int permille)
{
    if (!m_pendingFileName.isEmpty())
        m_indexProgress->setValue(permille);
}

void MainWindow::onIndexingFinished()
{
    m_indexProgress->reset();
    m_indexProgress->hide();

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

    // Every viewer is core-aware: hand out the source and filter. Opening a
    // file costs the line index, nothing else.
    m_pluginManager->setCoreSource(m_fileSource, m_lineIndex);
    m_pluginManager->setFilter(m_filterOptions);
    setWindowTitle(tr("Logdor - %1").arg(QFileInfo(fileName).fileName()));
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