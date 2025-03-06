#include "pythonlogviewer.h"
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QPainter>
#include <QRegularExpression>
#include <QStyle>
#include <QTextStream>
#include <algorithm>
#include <QtConcurrent/QtConcurrent>

PythonLogViewer::PythonLogViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_layout(new QVBoxLayout(m_container))
    , m_toolbar(new QToolBar())
    , m_tableView(new QTableView())
    , m_model(new PythonLogTableModel(this))
    , m_moduleComboBox(new QComboBox())
    , m_scrollArea(new QScrollArea())
    , m_modulesContainer(new QFrame())
    , m_modulesLayout(new QHBoxLayout(m_modulesContainer))
{
    setupUi();

    // Initialize level filters (all enabled by default)
    m_levelFilters[PythonLogEntry::Level::Debug] = true;
    m_levelFilters[PythonLogEntry::Level::Info] = true;
    m_levelFilters[PythonLogEntry::Level::Warning] = true;
    m_levelFilters[PythonLogEntry::Level::Error] = true;
    m_levelFilters[PythonLogEntry::Level::Critical] = true;
    m_levelFilters[PythonLogEntry::Level::Unknown] = true;
}

PythonLogViewer::~PythonLogViewer()
{
    delete m_container;
}

void PythonLogViewer::setupUi()
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->addWidget(m_toolbar);

    // Setup toolbar with level filter buttons
    auto createLevelIcon = [](const QColor& color, bool filtered) {
        QPixmap pixmap(16, 16);
        pixmap.fill(color);
        
        if (filtered) {
            QPainter painter(&pixmap);
            painter.setPen(QPen(Qt::white, 2));
            painter.drawLine(4, 4, 12, 12);
            painter.drawLine(12, 4, 4, 12);
        }
        
        return QIcon(pixmap);
    };

    auto addLevelAction = [this, createLevelIcon](PythonLogEntry::Level level) {
        QAction* action = new QAction(PythonLogEntry::levelToString(level), this);
        action->setCheckable(true);
        action->setChecked(true);
        QColor color = PythonLogEntry::levelColor(level);
        action->setIcon(createLevelIcon(color, false));
        connect(action, &QAction::toggled, this, [this, level, action, color, createLevelIcon](bool checked) {
            action->setIcon(createLevelIcon(color, !checked));
            toggleLevel(level, checked);
        });
        m_toolbar->addAction(action);
        m_levelActions[level] = action;
    };

    addLevelAction(PythonLogEntry::Level::Debug);
    addLevelAction(PythonLogEntry::Level::Info);
    addLevelAction(PythonLogEntry::Level::Warning);
    addLevelAction(PythonLogEntry::Level::Error);
    addLevelAction(PythonLogEntry::Level::Critical);
    addLevelAction(PythonLogEntry::Level::Unknown);

    // Setup module combobox
    m_moduleComboBox->setEditable(true);
    m_moduleComboBox->setInsertPolicy(QComboBox::InsertAlphabetically);
    m_moduleComboBox->setMinimumWidth(200);
    m_moduleComboBox->setPlaceholderText(tr("Filter by module..."));

    // Handle Enter key press in the combobox
    connect(m_moduleComboBox->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
        QString module = m_moduleComboBox->currentText().trimmed();
        if (!module.isEmpty() && !m_selectedModules.contains(module)) {
            addModuleLabel(module);
            m_moduleComboBox->setCurrentText("");
        }
    });

    // Handle item activation from dropdown
    connect(m_moduleComboBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        QString module = m_moduleComboBox->itemText(index).trimmed();
        if (!module.isEmpty() && !m_selectedModules.contains(module)) {
            addModuleLabel(module);
            m_moduleComboBox->setCurrentText("");
        }
    });

    // Setup scroll area for modules
    m_scrollArea->setWidget(m_modulesContainer);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFixedHeight(36);
    m_scrollArea->setFrameStyle(QFrame::NoFrame);

    // Setup modules container
    m_modulesContainer->setStyleSheet("QFrame { background: transparent; }");
    m_modulesLayout->setContentsMargins(0, 0, 0, 0);
    m_modulesLayout->setSpacing(2);
    m_modulesLayout->addStretch();

    // Add module label, combobox and container to toolbar
    m_toolbar->addSeparator();
    QLabel* moduleLabel = new QLabel(tr("Modules: "));
    m_toolbar->addWidget(moduleLabel);
    m_toolbar->addWidget(m_moduleComboBox);
    m_toolbar->addWidget(m_scrollArea);

    // Setup table view
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(PythonLogColumn::Message, QHeaderView::Stretch); // Message column stretches
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(false);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSortingEnabled(true);
    m_tableView->setFont(QFont("Monospace"));
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->sortByColumn(PythonLogColumn::No, Qt::AscendingOrder);
    
    // Connect selection changes to our handler
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &PythonLogViewer::onSelectionChanged);

    connect(m_tableView->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &PythonLogViewer::handleSort);

    m_layout->addWidget(m_tableView);
}

void PythonLogViewer::addModuleLabel(const QString& module)
{
    if (m_selectedModules.contains(module)) {
        return;
    }

    m_selectedModules.insert(module);
    QLabel* label = new QLabel(module);
    label->setStyleSheet("QLabel { background-color: #e0e0e0; padding: 2px 4px; border-radius: 2px; }");

    QPushButton* removeButton = new QPushButton("×");
    removeButton->setStyleSheet("QPushButton { border: none; padding: 0px 2px; }");
    removeButton->setFixedSize(16, 16);

    QWidget* container = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(label);
    layout->addWidget(removeButton);

    // Insert before the stretch
    m_modulesLayout->insertWidget(m_modulesLayout->count() - 1, container);

    connect(removeButton, &QPushButton::clicked, this, [this, container, module]() {
        m_selectedModules.remove(module);
        m_modulesLayout->removeWidget(container);
        container->deleteLater();
        updateVisibleRows();
    });

    updateVisibleRows();
}

bool PythonLogViewer::setLogs(const QList<LogEntry>& content)
{
    qDebug() << "PythonLogViewer::setLogs called with" << content.size() << "entries";
    m_entries = content;
    m_model->setLogEntries(content);

    // Clear selected modules
    while (m_modulesLayout->count() > 1) { // Keep the stretch
        QLayoutItem* item = m_modulesLayout->takeAt(0);
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_selectedModules.clear();

    // Start background module population
    QFuture<QSet<QString>> future = QtConcurrent::run([this]() {
        return getUniqueModules();
    });

    QFutureWatcher<QSet<QString>>* watcher = new QFutureWatcher<QSet<QString>>(this);
    connect(watcher, &QFutureWatcher<QSet<QString>>::finished, this, [this, watcher]() {
        QSet<QString> moduleSet = watcher->result();
        QList<QString> modules = moduleSet.values();
        std::sort(modules.begin(), modules.end());
        
        m_moduleComboBox->clear();
        for (const QString& module : modules) {
            m_moduleComboBox->addItem(module);
        }
        
        watcher->deleteLater();
    });

    watcher->setFuture(future);
    return true;
}

void PythonLogViewer::toggleLevel(PythonLogEntry::Level level, bool enabled)
{
    m_levelFilters[level] = enabled;
    updateVisibleRows();
}

void PythonLogViewer::updateVisibleRows()
{
    QSet<int> linesToShow;
    for (int i = 0; i < m_entries.size(); ++i) {
        PythonLogEntry entry(m_entries[i]);
        if (matchesFilter(entry)) {
            // Add context lines before
            for (int j = std::max(0, i - m_filterOptions.contextLinesBefore); j < i; ++j) {
                linesToShow.insert(j);
            }
            
            // Add the matching line
            linesToShow.insert(i);
            
            // Add context lines after
            for (int j = i + 1; j <= std::min<int>(m_entries.size() - 1, i + m_filterOptions.contextLinesAfter); ++j) {
                linesToShow.insert(j);
            }
        }
    }
    m_model->setVisibleRows(linesToShow.values().toVector());
}

void PythonLogViewer::setFilter(const FilterOptions& options)
{
    m_filterOptions = options;
    updateVisibleRows();
}

QList<FieldInfo> PythonLogViewer::availableFields() const
{
    return QList<FieldInfo>({
        {"No.", DataType::Integer},
        {"Time", DataType::DateTime},
        {"Level", DataType::String, QList<QVariant>({
            "Debug", "Info", "Warning", "Error", "Critical", "Unknown"
        })},
        {"Module", DataType::String},
        {"Line", DataType::Integer},
        {"Message", DataType::String}
    });
}

QSet<int> PythonLogViewer::filteredLines() const
{
    // TODO: Implement this method to return the indices of filtered out lines
    // For now, we return an empty set
    return QSet<int>();
}

void PythonLogViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    // TODO: Implement this method to synchronize filtered lines with other plugins
    // For now, we do nothing
    Q_UNUSED(lines);
}

void PythonLogViewer::handleSort(int column, Qt::SortOrder order)
{
    m_model->sort(column, order);
}

void PythonLogViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(deselected);
    Q_UNUSED(selected);
    
    QList<int> selectedLines;
    const auto indexes = m_tableView->selectionModel()->selectedRows();
    for (const auto& index : indexes) {
        selectedLines.append(m_model->mapToSourceRow(index.row()));
    }
    
    emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(selectedLines));
}

void PythonLogViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected) {
        QList<int> selectedLines = data.value<QList<int>>();
        if (selectedLines.isEmpty()) {
            return;
        }

        // For all selected lines, select them in the table view
        QItemSelection selection;
        for (int line : selectedLines) {
            const int row = m_model->mapFromSourceRow(line);
            selection.select(m_model->index(row, PythonLogColumn::No), m_model->index(row, PythonLogColumn::Message));
        }
        m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

        // Scroll to the first selected line
        if (!selectedLines.isEmpty()) {
            m_tableView->scrollTo(m_model->index(m_model->mapFromSourceRow(selectedLines.first()), 0));
        }
    }
}

QSet<QString> PythonLogViewer::getUniqueModules() const
{
    // Reduce function that safely combines modules into the result set
    auto reduceModules = [](QSet<QString>& result, const QString& module) {
        if (!module.isEmpty()) {
            result.insert(module);
        }
        return result;
    };

    // Process entries and reduce results in parallel
    return QtConcurrent::blockingMappedReduced<QSet<QString>>(
        m_entries,
        // Map function to extract modules
        [this](const LogEntry& entry) {
            PythonLogEntry pythonEntry(entry);
            return pythonEntry.module;
        },
        // Reduce function to combine results
        reduceModules,
        // Use parallel reduction
        QtConcurrent::ReduceOption::UnorderedReduce
    );
}

bool PythonLogViewer::matchesFilter(const PythonLogEntry& entry) const
{
    // Check level filter
    if (!m_levelFilters.value(entry.level, true)) {
        return false;
    }

    // Check module filters
    if (!m_selectedModules.isEmpty() && !m_selectedModules.contains(entry.module)) {
        return false;
    }

    // Check text filter
    if (!m_filterOptions.query.isEmpty()) {
        bool matches;
        
        if (m_filterOptions.inRegexMode) {
            // Regex mode
            QRegularExpression::PatternOptions patternOptions = QRegularExpression::NoPatternOption;
            if (m_filterOptions.caseSensitivity == Qt::CaseInsensitive) {
                patternOptions |= QRegularExpression::CaseInsensitiveOption;
            }
            QRegularExpression regex(m_filterOptions.query, patternOptions);
            matches = regex.match(entry.message).hasMatch();
        } else {
            // Normal text search mode
            matches = entry.message.contains(m_filterOptions.query, m_filterOptions.caseSensitivity);
        }
        
        if (matches == m_filterOptions.invertFilter) {
            return false;
        }
    }

    return true;
}
