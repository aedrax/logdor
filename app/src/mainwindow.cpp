#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "annotationexporter.h"
#include <QProgressDialog>
#include <QLabel>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <logdor/Query.h>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QMessageBox>
#include <QToolBar>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QShortcut>
#include <QRegularExpression>
#include <QStyle>
#include <QActionGroup>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QMenu>
#include <QTimeZone>
#include <QWidgetAction>
#include "foldersearchdock.h"
#include "folderview.h"
#include "followcontroller.h"
#include "formatcatalog.h"
#include "highlightrules.h"
#include "recentitems.h"
#include "timesettings.h"

#include <logdor/FormatRegistry.h>
#include <logdor/TimeProbe.h>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_pluginManager(new PluginManager(this))
    , m_filterToolbar(new FilterToolbar(this))
    , m_pluginsMenu(nullptr)
{
    ui->setupUi(this);
    m_pluginsMenu = ui->menuPlugins;
    // this allows the dock widget to use the full window
    this->setCentralWidget(nullptr);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onActionOpenTriggered);

    // Recents live between Open Folder and the Save actions. Rebuilt eagerly
    // (not on aboutToShow) so the Ctrl+1..9 shortcuts always work.
    m_recentMenu = new QMenu(tr("Open Recent"), this);
    ui->menuFile->insertMenu(ui->actionSaveAnnotations, m_recentMenu);
    ui->menuFile->insertSeparator(ui->actionSaveAnnotations);
    rebuildRecentMenu();

    connect(ui->actionSaveAnnotations, &QAction::triggered,
            this, &MainWindow::saveAnnotationsNow);
    connect(ui->actionSaveAnnotationsAs, &QAction::triggered,
            this, &MainWindow::saveAnnotationsAs);
    connect(ui->actionOpenFolder, &QAction::triggered,
            this, &MainWindow::onActionOpenFolderTriggered);

    // Cycle through the open folder's files; no-ops until a folder is open.
    auto* nextFileShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown), this);
    connect(nextFileShortcut, &QShortcut::activated, this, [this]() {
        if (m_folderView)
            m_folderView->selectNext();
    });
    auto* previousFileShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp), this);
    connect(previousFileShortcut, &QShortcut::activated, this, [this]() {
        if (m_folderView)
            m_folderView->selectPrevious();
    });

    // Filter toolbar. The row widget (FilterToolbar) owns every control:
    // QToolBar would hide overflowing individual items behind a ">>" popup
    // at narrow widths, while one row with a stretch guarantees the input
    // takes every pixel the fixed controls don't need.
    QToolBar* filterToolBar = addToolBar(tr("Filter"));
    filterToolBar->setObjectName("FilterToolBar"); // saveState participation
    filterToolBar->setMovable(false);
    filterToolBar->addWidget(m_filterToolbar);
    connect(m_filterToolbar, &FilterToolbar::filterChanged,
            this, &MainWindow::onFilterChanged);


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

    // Decompression stage for .gz opens; feeds the same progress dialog.
    m_openWatcher
        = new QFutureWatcher<logdor::FileSource::AsyncOpenResult>(this);
    connect(m_openWatcher, &QFutureWatcherBase::progressValueChanged,
            this, &MainWindow::onIndexingProgress);
    connect(m_openWatcher, &QFutureWatcherBase::finished,
            this, &MainWindow::onAsyncOpenFinished);

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
    // Window layout: same debounce idea, so geometry and dock state survive
    // exits that never reach closeEvent (logout SIGTERM, Ctrl+C, crashes).
    m_settingsSaveTimer = new QTimer(this);
    m_settingsSaveTimer->setSingleShot(true);
    m_settingsSaveTimer->setInterval(1000);
    connect(m_settingsSaveTimer, &QTimer::timeout,
            this, &MainWindow::saveSettings);

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

    // Zone-less timestamps (RFC3164 syslog, logcat, ...) are read in this
    // zone; changing it re-extracts every viewer's timestamp columns.
    ui->menuFile->addSeparator();
    QMenu* zoneMenu = ui->menuFile->addMenu(tr("Timestamp Time Zone"));
    auto* zoneGroup = new QActionGroup(this);
    QAction* systemZone = zoneMenu->addAction(tr("System Default"));
    QAction* utcZone = zoneMenu->addAction(tr("UTC"));
    QAction* chooseZone = zoneMenu->addAction(tr("Choose..."));
    for (QAction* action : { systemZone, utcZone, chooseZone }) {
        action->setCheckable(true);
        zoneGroup->addAction(action);
    }
    connect(systemZone, &QAction::triggered, this,
            []() { TimeSettings::instance().setAssumedZoneId({}); });
    connect(utcZone, &QAction::triggered, this,
            []() { TimeSettings::instance().setAssumedZoneId("UTC"); });
    connect(chooseZone, &QAction::triggered, this, [this]() {
        QStringList ids;
        for (const QByteArray& id : QTimeZone::availableTimeZoneIds())
            ids.append(QString::fromUtf8(id));
        const QString current
            = QString::fromUtf8(TimeSettings::instance().assumedZoneId());
        bool ok = false;
        const QString id = QInputDialog::getItem(
            this, tr("Timestamp Time Zone"),
            tr("Read zone-less timestamps as:"), ids,
            qMax(0, int(ids.indexOf(current))), false, &ok);
        if (ok && !id.isEmpty())
            TimeSettings::instance().setAssumedZoneId(id.toUtf8());
    });
    connect(zoneMenu, &QMenu::aboutToShow, this,
            [systemZone, utcZone, chooseZone]() {
                const QByteArray id = TimeSettings::instance().assumedZoneId();
                systemZone->setChecked(id.isEmpty());
                utcZone->setChecked(id == "UTC");
                const bool custom = !id.isEmpty() && id != "UTC";
                chooseZone->setChecked(custom);
                chooseZone->setText(custom
                                        ? tr("Choose... (%1)")
                                              .arg(QString::fromUtf8(id))
                                        : tr("Choose..."));
            });

    // App-wide highlight rules: a small manager dialog; rules render in
    // every viewer (first enabled match wins over severity coloring).
    QAction* highlightAction = ui->menuFile->addAction(
        tr("Highlight Rules..."));
    connect(highlightAction, &QAction::triggered, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Highlight Rules"));
        auto* dialogLayout = new QVBoxLayout(&dialog);
        auto* list = new QListWidget(&dialog);
        QList<HighlightRule> rules = loadHighlightRules();
        const auto addItem = [list](const HighlightRule& rule) {
            auto* item = new QListWidgetItem(rule.pattern, list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
            item->setBackground(rule.color);
            item->setForeground(Qt::black);
        };
        for (const HighlightRule& rule : std::as_const(rules))
            addItem(rule);
        dialogLayout->addWidget(list);
        auto* addButton = new QPushButton(tr("Add..."), &dialog);
        auto* removeButton = new QPushButton(tr("Remove"), &dialog);
        connect(addButton, &QPushButton::clicked, &dialog,
                [&dialog, list, &rules, addItem]() {
                    bool ok = false;
                    const QString pattern = QInputDialog::getText(
                        &dialog, tr("Add Highlight Rule"),
                        tr("Highlight lines containing:"), QLineEdit::Normal,
                        QString(), &ok);
                    if (!ok || pattern.trimmed().isEmpty())
                        return;
                    HighlightRule rule;
                    rule.name = pattern.trimmed().left(32);
                    rule.pattern = pattern.trimmed();
                    rule.color = nextHighlightColor(rules.size());
                    rules.append(rule);
                    addItem(rule);
                });
        connect(removeButton, &QPushButton::clicked, &dialog,
                [list, &rules]() {
                    const int row = list->currentRow();
                    if (row < 0)
                        return;
                    delete list->takeItem(row);
                    rules.removeAt(row);
                });
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected,
                &dialog, &QDialog::reject);
        auto* buttonRow = new QHBoxLayout();
        buttonRow->addWidget(addButton);
        buttonRow->addWidget(removeButton);
        buttonRow->addStretch();
        buttonRow->addWidget(buttons);
        dialogLayout->addLayout(buttonRow);
        dialog.exec();
        // Persist checkbox toggles and edits, then refan.
        for (int i = 0; i < rules.size() && i < list->count(); ++i)
            rules[i].enabled = list->item(i)->checkState() == Qt::Checked;
        storeHighlightRules(rules);
        m_pluginManager->setHighlightRules(rules);
    });

    // Folder-wide search (Ctrl+Shift+F), dock created on first use.
    QAction* searchAction = ui->menuFile->addAction(tr("Search in Folder..."));
    searchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(searchAction, &QAction::triggered, this, [this]() {
        ensureSearchDock();
        m_searchDock->show();
        m_searchDock->raise();
        m_searchDock->focusPattern();
    });

    // Follow mode: tail the current file, extending viewers in place.
    ui->menuFile->addSeparator();
    m_followAction = ui->menuFile->addAction(tr("Follow File"));
    m_followAction->setCheckable(true);
    m_followAction->setShortcut(QKeySequence(Qt::Key_F8));
    m_followAction->setEnabled(false); // until a file finishes indexing
    connect(m_followAction, &QAction::toggled,
            this, &MainWindow::onFollowToggled);
    m_followController = new FollowController(this);
    connect(m_followController, &FollowController::extended,
            this, &MainWindow::onFollowExtended);
    connect(m_followController, &FollowController::rotated,
            this, &MainWindow::onFollowRotated);

    // Indexing progress: modal-less, cancellable, only appears past 1 s.
    m_indexProgress = new QProgressDialog(this);
    m_indexProgress->setWindowModality(Qt::NonModal);
    m_indexProgress->setRange(0, 1000); // permille from buildLineIndex
    m_indexProgress->setMinimumDuration(1000);
    m_indexProgress->setAutoClose(false);
    m_indexProgress->setAutoReset(false);
    m_indexProgress->reset();
    connect(m_indexProgress, &QProgressDialog::canceled, this, [this]() {
        m_openWatcher->cancel();
        m_indexWatcher->cancel();
    });

    loadPlugins();
    m_pluginManager->setHighlightRules(loadHighlightRules());
    loadSettings();
    loadSessionsFile();
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

        // Dock rearrangements persist without waiting for a clean close.
        connect(dock, &QDockWidget::dockLocationChanged,
                this, &MainWindow::scheduleSettingsSave);
        connect(dock, &QDockWidget::topLevelChanged,
                this, &MainWindow::scheduleSettingsSave);
        connect(dock, &QDockWidget::visibilityChanged,
                this, &MainWindow::scheduleSettingsSave);

        // Viewers push "add to filter" terms up to the shared filter bar.
        connect(plugin, &PluginInterface::filterTermRequested,
                this, &MainWindow::onFilterTermRequested);

        // "Highlight lines like this": store a rule and refan the set.
        connect(plugin, &PluginInterface::highlightRequested, this,
                [this](const QString& pattern) {
                    QList<HighlightRule> rules = loadHighlightRules();
                    HighlightRule rule;
                    rule.name = pattern.left(32);
                    rule.pattern = pattern;
                    rule.color = nextHighlightColor(rules.size());
                    rules.append(rule);
                    storeHighlightRules(rules);
                    m_pluginManager->setHighlightRules(rules);
                });

        // Histogram brushes become @time terms (replace, never stack).
        connect(plugin, &PluginInterface::timeRangeRequested, this,
                [this](qint64 fromUtcMs, qint64 toUtcMs) {
                    if (fromUtcMs == 0 && toUtcMs == 0) {
                        applyTimeRange({});
                        return;
                    }
                    const auto format = [](qint64 ms) {
                        return logdor::quoteQueryValue(
                            QDateTime::fromMSecsSinceEpoch(ms).toString(
                                u"yyyy-MM-dd HH:mm:ss.zzz"),
                            /*forceQuote=*/true);
                    };
                    applyTimeRange(
                        { QStringLiteral("@time>=%1").arg(format(fromUtcMs)),
                          QStringLiteral("@time<=%1").arg(format(toUtcMs)) });
                });
        
        m_pluginsMenu->addAction(action);
        m_pluginActions[pluginName] = action;
    }
}

void MainWindow::addRecentItem(const QString& path)
{
    QSettings settings("Logdor", "Logdor");
    settings.setValue("recentItems",
                      updatedRecents(
                          settings.value("recentItems").toStringList(), path));
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    QSettings settings("Logdor", "Logdor");
    const QStringList stored = settings.value("recentItems").toStringList();
    const QStringList items = prunedRecents(stored);
    if (items != stored)
        settings.setValue("recentItems", items);

    m_recentMenu->clear();
    m_recentMenu->setEnabled(!items.isEmpty());

    for (int i = 0; i < items.size(); ++i) {
        const QString& path = items.at(i);
        const QFileInfo info(path);
        QAction* action = m_recentMenu->addAction(
            tr("&%1  %2 - %3")
                .arg(i + 1)
                .arg(info.fileName(), QDir::toNativeSeparators(info.path())));
        if (i < 9)
            action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        action->setIcon(style()->standardIcon(
            info.isDir() ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        connect(action, &QAction::triggered, this,
                [this, path]() { openPath(path); });
    }

    m_recentMenu->addSeparator();
    QAction* clear = m_recentMenu->addAction(tr("Clear Recent"));
    clear->setEnabled(!items.isEmpty());
    connect(clear, &QAction::triggered, this, [this]() {
        QSettings settings("Logdor", "Logdor");
        settings.remove("recentItems");
        rebuildRecentMenu();
    });
}

void MainWindow::openPath(const QString& path)
{
    if (QFileInfo(path).isDir())
        openFolder(path);
    else
        openFile(path);
}

void MainWindow::ensureSearchDock()
{
    if (m_searchDock)
        return;
    m_searchDock = new FolderSearchDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_searchDock);
    // Created after loadSettings' restoreState, so apply the saved layout
    // (area, size, floating) to it explicitly.
    restoreDockWidget(m_searchDock);
    connect(m_searchDock, &QDockWidget::dockLocationChanged,
            this, &MainWindow::scheduleSettingsSave);
    connect(m_searchDock, &QDockWidget::topLevelChanged,
            this, &MainWindow::scheduleSettingsSave);
    if (m_folderView)
        m_searchDock->setFolder(m_folderView->folder());
    else if (!m_currentFileName.isEmpty())
        m_searchDock->setFolder(QFileInfo(m_currentFileName).absolutePath());
    connect(m_searchDock, &FolderSearchDock::openRequested,
            this, &MainWindow::openFileAtLine);
    connect(m_searchDock, &FolderSearchDock::addToTimelineRequested, this,
            [this](const QString& path) {
                // Reveal the timeline first: showing its dock enables the
                // plugin, and only enabled plugins receive events.
                if (QDockWidget* dock
                    = m_pluginDocks.value(tr("Merged Timeline"))) {
                    dock->show();
                    dock->raise();
                }
                m_pluginManager->broadcastEvent(
                    PluginEvent::AddFileToTimeline, path);
            });
}

void MainWindow::openFileAtLine(const QString& fileName, qint64 line)
{
    if (QFileInfo(fileName).canonicalFilePath()
        == QFileInfo(m_currentFileName).canonicalFilePath()) {
        // Already open: select immediately.
        m_pluginManager->broadcastEvent(
            PluginEvent::LinesSelected,
            QVariant::fromValue(QList<int> { int(line) }));
        return;
    }
    m_pendingJumpLine = line;
    if (!openFile(fileName))
        m_pendingJumpLine = -1;
}

void MainWindow::openFolder(const QString& dir)
{
    if (!m_folderView) {
        m_folderView = new FolderView(this);
        m_folderDock = new QDockWidget(tr("Files"), this);
        m_folderDock->setObjectName("FolderView"); // saveState participation
        m_folderDock->setWidget(m_folderView);
        addDockWidget(Qt::LeftDockWidgetArea, m_folderDock);
        // Created after loadSettings' restoreState, so apply the saved
        // layout (area, size, floating) to it explicitly.
        restoreDockWidget(m_folderDock);
        connect(m_folderDock, &QDockWidget::dockLocationChanged,
                this, &MainWindow::scheduleSettingsSave);
        connect(m_folderDock, &QDockWidget::topLevelChanged,
                this, &MainWindow::scheduleSettingsSave);
        connect(m_folderView, &FolderView::fileActivated,
                this, &MainWindow::openFile);
    }
    m_folderView->setFolder(dir);
    m_folderDock->setWindowTitle(
        tr("Files - %1").arg(QFileInfo(dir).fileName()));
    m_folderDock->show();
    m_folderDock->raise();
    if (m_searchDock)
        m_searchDock->setFolder(dir);
    addRecentItem(dir);
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

void MainWindow::scheduleSettingsSave()
{
    m_settingsSaveTimer->start();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    scheduleSettingsSave();
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    scheduleSettingsSave();
}

void MainWindow::loadSettings()
{
    QSettings settings("Logdor", "Logdor");

    // Versioned so future layout changes can migrate old settings.
    settings.setValue("meta/settingsVersion", 1);

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
    // Capture the open file's session BEFORE teardown so quitting keeps its
    // state for the next run (the historical gap: only file SWITCHES did).
    if (!m_currentFileName.isEmpty() && m_lineIndex)
        captureSession();
    flushAnnotationSave();
    saveSettings();
    QMainWindow::closeEvent(event);
}

bool MainWindow::openFile(const QString& fileName)
{
    // `logdor /var/log` and folder recents route to the folder view.
    if (QFileInfo(fileName).isDir()) {
        openFolder(fileName);
        return true;
    }

    // Following is per-file; a new open (or rotation reload) restarts it.
    m_followController->stop();
    {
        const QSignalBlocker blockFollow(m_followAction);
        m_followAction->setChecked(false);
    }
    m_followAction->setEnabled(false);

    // Snapshot the outgoing file's view state before teardown wipes it -
    // viewers clear selection/sort in setCoreSource(nullptr, nullptr).
    if (!m_currentFileName.isEmpty() && m_lineIndex)
        captureSession();

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

    // Clear plugins' data before dropping the previous mapping - they hold
    // shared_ptrs into it.
    m_pluginManager->setCoreSource(nullptr, nullptr);
    m_lineIndex.reset();
    m_fileSource.reset();

    // Compressed files inflate off-thread first, then chain into the same
    // indexing flow; failures surface from onAsyncOpenFinished().
    if (m_openWatcher->isRunning())
        m_openWatcher->cancel();
    if (logdor::FileSource::isCompressedFile(fileName)) {
        m_pendingFileName = fileName;
        setWindowTitle(tr("Logdor - %1 (Decompressing...)")
                           .arg(QFileInfo(fileName).fileName()));
        m_indexProgress->setLabelText(
            tr("Decompressing %1...").arg(QFileInfo(fileName).fileName()));
        m_indexProgress->setValue(0);
        m_openWatcher->setFuture(logdor::FileSource::openAsync(fileName));
        return true;
    }

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
    startIndexingCurrentFile();

    // true means "open + indexing started"; failures after this point are
    // reported asynchronously from onIndexingFinished().
    return true;
}

void MainWindow::startIndexingCurrentFile()
{
    setWindowTitle(tr("Logdor - %1 (Indexing...)")
                       .arg(QFileInfo(m_pendingFileName).fileName()));
    m_indexProgress->setLabelText(
        tr("Indexing %1...").arg(QFileInfo(m_pendingFileName).fileName()));
    m_indexProgress->setValue(0);
    m_indexWatcher->setFuture(logdor::buildLineIndex(m_fileSource));
}

void MainWindow::onAsyncOpenFinished()
{
    if (m_openWatcher->future().isCanceled()) {
        m_indexProgress->reset();
        m_indexProgress->hide();
        m_pendingFileName.clear();
        setWindowTitle(tr("Logdor"));
        ui->statusbar->showMessage(tr("Open cancelled"), 3000);
        return;
    }
    const logdor::FileSource::AsyncOpenResult result
        = m_openWatcher->future().result();
    if (!result.source) {
        m_indexProgress->reset();
        m_indexProgress->hide();
        m_pendingFileName.clear();
        setWindowTitle(tr("Logdor"));
        QMessageBox::warning(this, tr("Error"), result.error);
        return;
    }
    m_fileSource = result.source;
    startIndexingCurrentFile();
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

    const char* modeName = "buffered";
    if (m_fileSource->mode() == logdor::FileSource::Mode::Mapped)
        modeName = "mapped";
    else if (m_fileSource->mode() == logdor::FileSource::Mode::Decompressed)
        modeName = "decompressed";
    qDebug() << "Indexed" << result.lineCount << "lines in" << result.elapsedMs
             << "ms," << modeName;
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
    // Revisited files come back the way they were left; first visits keep
    // the current toolbar filter. setFilter() below applies either in one
    // shot, and viewers finish their part when their scans land.
    restoreSession(fileName);
    // Seed the time picker with this file's observed span (cheap head/tail
    // probe with the auto-detected format; monotonic clocks don't seed).
    {
        static const auto parsers = loadAllParsers();
        logdor::TimeRangeProbe probe;
        const auto scores = logdor::detectFormat(*m_fileSource, *m_lineIndex,
                                                 parsers);
        if (!scores.isEmpty()) {
            if (const auto parser
                = logdor::parserById(scores.front().parserId, parsers)) {
                probe = logdor::probeTimeRange(
                    *m_fileSource, *m_lineIndex, *parser,
                    TimeSettings::instance().contextForFile(fileName));
            }
        }
        if (probe.valid && !probe.monotonic)
            m_filterToolbar->setTimeRangeHint(probe.firstMs, probe.lastMs);
        else
            m_filterToolbar->setTimeRangeHint(0, 0);
    }

    m_pluginManager->setFilter(m_filterOptions);
    if (m_pendingJumpLine >= 0) {
        // Folder-search jump: viewers apply the selection once their own
        // scans land (selectSourceLines is async-safe).
        m_pluginManager->broadcastEvent(
            PluginEvent::LinesSelected,
            QVariant::fromValue(QList<int> { int(m_pendingJumpLine) }));
        m_pendingJumpLine = -1;
    }
    setWindowTitle(tr("Logdor - %1").arg(QFileInfo(fileName).fileName()));
    addRecentItem(fileName); // success only: failed opens never enter recents
    if (m_folderView)
        m_folderView->setCurrentFile(fileName); // no echo: highlight only

    // Following a decompressed snapshot is meaningless - the on-disk bytes
    // are not the displayed bytes.
    m_followAction->setEnabled(m_fileSource->mode()
                               != logdor::FileSource::Mode::Decompressed);
    if (m_refollowAfterLoad) {
        m_refollowAfterLoad = false;
        if (m_followAction->isEnabled())
            m_followAction->setChecked(true); // rotation reload keeps following
    }
}

void MainWindow::onFollowToggled(bool on)
{
    if (on && m_fileSource && m_lineIndex && !m_currentFileName.isEmpty()) {
        m_followController->start(m_currentFileName, m_fileSource,
                                  m_lineIndex);
    } else {
        m_followController->stop();
    }
}

void MainWindow::onFollowExtended(std::shared_ptr<logdor::FileSource> source,
                                  std::shared_ptr<const logdor::LineIndex> index,
                                  qint64 firstNewLine)
{
    // Adopt the grown snapshot as the current file and extend everyone in
    // place; the old mapping stays alive for any straggling readers.
    m_fileSource = source;
    m_lineIndex = index;
    m_annotationHub->extendFile(source, index);
    m_pluginManager->extendCoreSource(std::move(source), std::move(index),
                                      firstNewLine);
    ui->statusbar->showMessage(tr("Following - %L1 lines")
                                   .arg(m_lineIndex->lineCount()),
                               2000);
}

void MainWindow::onFollowRotated()
{
    // The controller already stopped itself. Reload from scratch and pick
    // follow back up once the fresh index lands.
    const QString fileName = m_currentFileName;
    ui->statusbar->showMessage(tr("File rotated - reloading"), 3000);
    m_refollowAfterLoad = !fileName.isEmpty();
    if (!fileName.isEmpty() && !openFile(fileName))
        m_refollowAfterLoad = false;
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

QString MainWindow::flushAnnotationSave()
{
    if (!m_annotationHub->hasFile() || !m_annotationHub->isDirty())
        return {};

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
        return annotationSidecarPath();
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
        return fallback;
    }
    ui->statusbar->showMessage(
        tr("FAILED to save annotations - check disk space and permissions"),
        10000);
    return {};
}

void MainWindow::saveAnnotationsNow()
{
    if (!m_annotationHub->hasFile()) {
        ui->statusbar->showMessage(tr("Open a log file first"), 3000);
        return;
    }
    m_annotationSaveTimer->stop();
    if (!m_annotationHub->isDirty()) {
        ui->statusbar->showMessage(tr("No unsaved annotation changes"), 3000);
        return;
    }
    const QString path = flushAnnotationSave();
    if (!path.isEmpty())
        ui->statusbar->showMessage(tr("Annotations saved to %1").arg(path), 5000);
    // Failure already reported by flushAnnotationSave().
}

void MainWindow::saveAnnotationsAs()
{
    // A one-time copy for sharing/backup (pairs with Import). Autosave keeps
    // targeting the canonical sidecar so annotations still auto-load.
    if (!m_annotationHub->hasFile()) {
        ui->statusbar->showMessage(tr("Open a log file first"), 3000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Annotations As"), annotationSidecarPath(),
        tr("Logdor annotations (*.logdor.json);;All files (*)"));
    if (path.isEmpty())
        return;

    m_annotationSaveTimer->stop();
    flushAnnotationSave(); // canonical sidecar and the copy never diverge

    const QByteArray bytes = logdor::saveAnnotations(
        m_annotationHub->set(), m_annotationHub->identity(),
        QFileInfo(m_currentFileName).fileName());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || (file.write(bytes), !file.commit())) {
        QMessageBox::warning(this, tr("Save Annotations As"),
                             tr("Could not write %1").arg(path));
        return;
    }
    ui->statusbar->showMessage(tr("Annotations saved to %1").arg(path), 5000);
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

QString MainWindow::sessionKey(const QString& fileName)
{
    const QString canonical = QFileInfo(fileName).canonicalFilePath();
    return canonical.isEmpty() ? fileName : canonical;
}

void MainWindow::captureSession()
{
    FileSession session;
    session.filter = m_filterOptions;
    session.timeRangeTerms = m_filterToolbar->timeRangeTerms();
    session.pluginStates = m_pluginManager->saveViewStates();
    if (m_fileSource)
        session.identity = logdor::computeFileIdentity(*m_fileSource);
    session.lastUsed = QDateTime::currentMSecsSinceEpoch();
    m_sessions.insert(sessionKey(m_currentFileName), session);
    saveSessionsFile(); // small file; write-behind on every switch
}

void MainWindow::restoreSession(const QString& fileName)
{
    const auto it = m_sessions.constFind(sessionKey(fileName));
    if (it == m_sessions.constEnd())
        return;
    const FileSession& session = it.value();

    // Across app runs the file may have been rotated or replaced: a saved
    // identity that mismatches the current content drops the session
    // (Grown - the log was appended to - still restores).
    if (session.identity.isValid() && m_fileSource
        && logdor::matchIdentity(session.identity, *m_fileSource)
            == logdor::IdentityMatch::Mismatch) {
        m_sessions.remove(sessionKey(fileName));
        return;
    }

    // Write the file's filter back into the toolbar without triggering the
    // debounce; the caller's setFilter() applies it in one shot.
    m_filterToolbar->setOptionsSilently(session.filter);
    m_filterToolbar->setTimeRangeTermsSilently(session.timeRangeTerms);
    m_filterOptions = session.filter;

    m_pluginManager->restoreViewStates(session.pluginStates);
}

QString MainWindow::sessionsFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/sessions.json");
}

void MainWindow::loadSessionsFile()
{
    QFile file(sessionsFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != 1)
        return;
    const QJsonObject sessions
        = root.value(QStringLiteral("sessions")).toObject();
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QJsonObject filter
            = entry.value(QStringLiteral("filter")).toObject();
        FileSession session;
        session.filter = FilterOptions(
            filter.value(QStringLiteral("query")).toString(),
            filter.value(QStringLiteral("before")).toInt(),
            filter.value(QStringLiteral("after")).toInt(),
            filter.value(QStringLiteral("caseSensitive")).toBool()
                ? Qt::CaseSensitive
                : Qt::CaseInsensitive,
            filter.value(QStringLiteral("invert")).toBool(),
            filter.value(QStringLiteral("queryMode")).toBool(),
            filter.value(QStringLiteral("regexMode")).toBool());
        const QJsonArray terms
            = entry.value(QStringLiteral("timeRangeTerms")).toArray();
        for (const QJsonValue& term : terms)
            session.timeRangeTerms.append(term.toString());
        session.pluginStates
            = entry.value(QStringLiteral("pluginStates")).toObject();
        const QJsonObject identity
            = entry.value(QStringLiteral("identity")).toObject();
        session.identity.size
            = quint64(identity.value(QStringLiteral("size")).toDouble());
        session.identity.prefixSha256 = identity.value(QStringLiteral("sha"))
                                            .toString()
                                            .toUtf8();
        session.identity.prefixLength
            = quint32(identity.value(QStringLiteral("prefixLength")).toInt());
        session.lastUsed
            = qint64(entry.value(QStringLiteral("lastUsed")).toDouble());
        m_sessions.insert(it.key(), session);
    }
}

void MainWindow::saveSessionsFile() const
{
    // LRU-cap the persisted set so the file stays small forever.
    constexpr int kMaxSessions = 50;
    QList<QString> keys = m_sessions.keys();
    std::sort(keys.begin(), keys.end(),
              [this](const QString& a, const QString& b) {
                  return m_sessions[a].lastUsed > m_sessions[b].lastUsed;
              });
    if (keys.size() > kMaxSessions)
        keys = keys.mid(0, kMaxSessions);

    QJsonObject sessions;
    for (const QString& key : std::as_const(keys)) {
        const FileSession& session = m_sessions[key];
        QJsonObject filter;
        filter.insert(QStringLiteral("query"), session.filter.query);
        filter.insert(QStringLiteral("queryMode"), session.filter.inQueryMode);
        filter.insert(QStringLiteral("regexMode"), session.filter.inRegexMode);
        filter.insert(QStringLiteral("caseSensitive"),
                      session.filter.caseSensitivity == Qt::CaseSensitive);
        filter.insert(QStringLiteral("invert"), session.filter.invertFilter);
        filter.insert(QStringLiteral("before"),
                      session.filter.contextLinesBefore);
        filter.insert(QStringLiteral("after"),
                      session.filter.contextLinesAfter);
        QJsonObject identity;
        identity.insert(QStringLiteral("size"),
                        double(session.identity.size));
        identity.insert(QStringLiteral("sha"),
                        QString::fromUtf8(session.identity.prefixSha256));
        identity.insert(QStringLiteral("prefixLength"),
                        int(session.identity.prefixLength));
        QJsonObject entry;
        entry.insert(QStringLiteral("filter"), filter);
        entry.insert(QStringLiteral("timeRangeTerms"),
                     QJsonArray::fromStringList(session.timeRangeTerms));
        entry.insert(QStringLiteral("pluginStates"), session.pluginStates);
        entry.insert(QStringLiteral("identity"), identity);
        entry.insert(QStringLiteral("lastUsed"), double(session.lastUsed));
        sessions.insert(key, entry);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("sessions"), sessions);

    QDir().mkpath(QFileInfo(sessionsFilePath()).absolutePath());
    QSaveFile file(sessionsFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

void MainWindow::onActionOpenTriggered()
{
    QFileDialog fileDialog(this, tr("Open File"), QString(),
                           tr("Log files (*.log *.txt *.json *.gz);;"
                              "All Files (*)"));
    fileDialog.selectNameFilter(tr("All Files (*)"));
    while (fileDialog.exec() == QDialog::Accepted
        && !openFile(fileDialog.selectedFiles().constFirst())) {
    }
}

void MainWindow::onActionOpenFolderTriggered()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Open Folder"));
    if (!dir.isEmpty())
        openFolder(dir);
}

void MainWindow::onFocusFilterInput()
{
    m_filterToolbar->focusInput();
}

void MainWindow::onFilterChanged(const FilterOptions& options)
{
    m_filterOptions = options;
    m_pluginManager->setFilter(m_filterOptions);
}

void MainWindow::applyTimeRange(const QStringList& terms)
{
    m_filterToolbar->applyTimeRange(terms);
}

void MainWindow::onFilterTermRequested(const QString& term)
{
    m_filterToolbar->appendTerm(term);
}

