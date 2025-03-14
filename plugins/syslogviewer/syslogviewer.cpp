#include "syslogviewer.h"
#include <QtWidgets/QHeaderView>
#include <QtGui/QIcon>
#include <QtWidgets/QLineEdit>
#include <QtGui/QPainter>
#include <QtCore/QRegularExpression>
#include <QtWidgets/QStyle>
#include <QtCore/QTextStream>
#include <algorithm>
#include <QtConcurrent/QtConcurrent>

SyslogViewer::SyslogViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_layout(new QVBoxLayout(m_container))
    , m_toolbar(new QToolBar())
    , m_tableView(new QTableView())
    , m_model(new SyslogTableModel(this))
    , m_facilityComboBox(new QComboBox())
    , m_scrollArea(new QScrollArea())
    , m_facilitiesContainer(new QFrame())
    , m_facilitiesLayout(new QHBoxLayout(m_facilitiesContainer))
{
    setupUi();

    // Initialize severity filters (all enabled by default)
    m_severityFilters[SyslogEntry::Severity::Emergency] = true;
    m_severityFilters[SyslogEntry::Severity::Alert] = true;
    m_severityFilters[SyslogEntry::Severity::Critical] = true;
    m_severityFilters[SyslogEntry::Severity::Error] = true;
    m_severityFilters[SyslogEntry::Severity::Warning] = true;
    m_severityFilters[SyslogEntry::Severity::Notice] = true;
    m_severityFilters[SyslogEntry::Severity::Info] = true;
    m_severityFilters[SyslogEntry::Severity::Debug] = true;
    m_severityFilters[SyslogEntry::Severity::Unknown] = true;
}

SyslogViewer::~SyslogViewer()
{
    delete m_container;
}

void SyslogViewer::setupUi()
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->addWidget(m_toolbar);

    // Setup toolbar with severity filter buttons
    auto createSeverityIcon = [](const QColor& color, bool filtered) {
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

    auto addSeverityAction = [this, createSeverityIcon](SyslogEntry::Severity severity) {
        QAction* action = new QAction(SyslogEntry::severityToString(severity), this);
        action->setCheckable(true);
        action->setChecked(true);
        QColor color = SyslogEntry::severityColor(severity);
        action->setIcon(createSeverityIcon(color, false));
        connect(action, &QAction::toggled, this, [this, severity, action, color, createSeverityIcon](bool checked) {
            action->setIcon(createSeverityIcon(color, !checked));
            toggleSeverity(severity, checked);
        });
        m_toolbar->addAction(action);
        m_severityActions[severity] = action;
    };

    addSeverityAction(SyslogEntry::Severity::Emergency);
    addSeverityAction(SyslogEntry::Severity::Alert);
    addSeverityAction(SyslogEntry::Severity::Critical);
    addSeverityAction(SyslogEntry::Severity::Error);
    addSeverityAction(SyslogEntry::Severity::Warning);
    addSeverityAction(SyslogEntry::Severity::Notice);
    addSeverityAction(SyslogEntry::Severity::Info);
    addSeverityAction(SyslogEntry::Severity::Debug);
    addSeverityAction(SyslogEntry::Severity::Unknown);

    // Setup facility combobox
    m_facilityComboBox->setEditable(true);
    m_facilityComboBox->setInsertPolicy(QComboBox::InsertAlphabetically);
    m_facilityComboBox->setMinimumWidth(200);
    m_facilityComboBox->setPlaceholderText(tr("Filter by facility..."));

    // Handle Enter key press in the combobox
    connect(m_facilityComboBox->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
        QString facility = m_facilityComboBox->currentText().trimmed();
        if (!facility.isEmpty() && !m_selectedFacilities.contains(facility)) {
            addFacilityLabel(facility);
            m_facilityComboBox->setCurrentText("");
        }
    });

    // Handle item activation from dropdown
    connect(m_facilityComboBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        QString facility = m_facilityComboBox->itemText(index).trimmed();
        if (!facility.isEmpty() && !m_selectedFacilities.contains(facility)) {
            addFacilityLabel(facility);
            m_facilityComboBox->setCurrentText("");
        }
    });

    // Setup scroll area for facilities
    m_scrollArea->setWidget(m_facilitiesContainer);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFixedHeight(36);
    m_scrollArea->setFrameStyle(QFrame::NoFrame);

    // Setup facilities container
    m_facilitiesContainer->setStyleSheet("QFrame { background: transparent; }");
    m_facilitiesLayout->setContentsMargins(0, 0, 0, 0);
    m_facilitiesLayout->setSpacing(2);
    m_facilitiesLayout->addStretch();

    // Add facility label, combobox and container to toolbar
    m_toolbar->addSeparator();
    QLabel* facilityLabel = new QLabel(tr("Facilities: "));
    m_toolbar->addWidget(facilityLabel);
    m_toolbar->addWidget(m_facilityComboBox);
    m_toolbar->addWidget(m_scrollArea);

    // Setup table view
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(SyslogColumn::Message, QHeaderView::Stretch); // Message column stretches
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(false);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSortingEnabled(true);
    m_tableView->setFont(QFont("Monospace"));
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->sortByColumn(SyslogColumn::No, Qt::AscendingOrder);
    
    // Connect selection changes to our handler
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SyslogViewer::onSelectionChanged);

    connect(m_tableView->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &SyslogViewer::handleSort);

    m_layout->addWidget(m_tableView);
}

void SyslogViewer::addFacilityLabel(const QString& facility)
{
    if (m_selectedFacilities.contains(facility)) {
        return;
    }

    m_selectedFacilities.insert(facility);
    
    // Create a label with a close button
    QFrame* labelFrame = new QFrame(m_facilitiesContainer);
    labelFrame->setStyleSheet("QFrame { background: #e0e0e0; border-radius: 3px; }");
    QHBoxLayout* labelLayout = new QHBoxLayout(labelFrame);
    labelLayout->setContentsMargins(6, 2, 6, 2);
    labelLayout->setSpacing(4);
    
    QLabel* textLabel = new QLabel(facility);
    QPushButton* closeButton = new QPushButton("×");
    closeButton->setFlat(true);
    closeButton->setMaximumSize(16, 16);
    closeButton->setStyleSheet("QPushButton { border: none; }");
    
    labelLayout->addWidget(textLabel);
    labelLayout->addWidget(closeButton);

    // Insert before the stretch
    m_facilitiesLayout->insertWidget(m_facilitiesLayout->count() - 1, labelFrame);

    connect(closeButton, &QPushButton::clicked, this, [this, labelFrame, facility]() {
        m_selectedFacilities.remove(facility);
        m_facilitiesLayout->removeWidget(labelFrame);
        labelFrame->deleteLater();
        updateVisibleRows();
    });

    updateVisibleRows();
}

bool SyslogViewer::setLogs(const QList<LogEntry>& content)
{
    qDebug() << "SyslogViewer::setLogs called with" << content.size() << "entries";
    m_entries = content;
    m_model->setLogEntries(content);

    // Clear selected facilities
    while (m_facilitiesLayout->count() > 1) { // Keep the stretch
        QLayoutItem* item = m_facilitiesLayout->takeAt(0);
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_selectedFacilities.clear();

    // Start background facility population
    QFuture<QSet<QString>> future = QtConcurrent::run([this]() {
        return getUniqueFacilities();
    });

    QFutureWatcher<QSet<QString>>* watcher = new QFutureWatcher<QSet<QString>>(this);
    connect(watcher, &QFutureWatcher<QSet<QString>>::finished, this, [this, watcher]() {
        QSet<QString> facilitySet = watcher->result();
        QList<QString> facilities = facilitySet.values();
        std::sort(facilities.begin(), facilities.end());
        
        m_facilityComboBox->clear();
        for (const QString& facility : facilities) {
            m_facilityComboBox->addItem(facility);
        }
        
        watcher->deleteLater();
    });

    watcher->setFuture(future);
    return true;
}

void SyslogViewer::toggleSeverity(SyslogEntry::Severity severity, bool enabled)
{
    m_severityFilters[severity] = enabled;
    updateVisibleRows();
}

void SyslogViewer::toggleFacility(const QString& facility, bool enabled)
{
    if (enabled) {
        m_selectedFacilities.insert(facility);
    } else {
        m_selectedFacilities.remove(facility);
    }
    updateVisibleRows();
}

void SyslogViewer::updateVisibleRows()
{
    // Disconnect sort signal temporarily to prevent recursive model resets
    disconnect(m_tableView->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
              this, &SyslogViewer::handleSort);

    QSet<int> linesToShow;
    for (int i = 0; i < m_entries.size(); ++i) {
        SyslogEntry entry(m_entries[i]);
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

    // Update visible rows with current sort settings
    QList<int> visibleRows = linesToShow.values();
    m_model->setVisibleRows(visibleRows);

    // Reconnect sort signal
    connect(m_tableView->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &SyslogViewer::handleSort);
}

void SyslogViewer::setFilter(const FilterOptions& options)
{
    m_filterOptions = options;
    updateVisibleRows();
}

QList<FieldInfo> SyslogViewer::availableFields() const
{
    return QList<FieldInfo>({
        {"No.", DataType::Integer},
        {"Time", DataType::DateTime},
        {"Hostname", DataType::String},
        {"Process", DataType::String},
        {"Facility", DataType::String, QList<QVariant>({
            "kern", "user", "mail", "daemon", "auth", "syslog", "lpr", "news",
            "uucp", "cron", "authpriv", "ftp", "ntp", "logaudit", "logalert",
            "clock", "local0", "local1", "local2", "local3", "local4", "local5",
            "local6", "local7", "unknown"
        })},
        {"Severity", DataType::String, QList<QVariant>({
            "Emergency", "Alert", "Critical", "Error", "Warning", "Notice", "Info",
            "Debug", "Unknown"
        })},
        {"Message", DataType::String}
    });
}

QSet<int> SyslogViewer::filteredLines() const
{
    QSet<int> filtered;
    for (int i = 0; i < m_entries.size(); ++i) {
        SyslogEntry entry(m_entries[i]);
        if (!matchesFilter(entry)) {
            filtered.insert(i);
        }
    }
    return filtered;
}

void SyslogViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    QList<int> visibleLines;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (!lines.contains(i)) {
            visibleLines.append(i);
        }
    }
    m_model->setVisibleRows(visibleLines);
}

void SyslogViewer::handleSort(int column, Qt::SortOrder order)
{
    m_model->sort(column, order);
}

void SyslogViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
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

void SyslogViewer::onPluginEvent(PluginEvent event, const QVariant& data)
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
            selection.select(m_model->index(row, SyslogColumn::No), m_model->index(row, SyslogColumn::Message));
        }
        m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

        // Scroll to the first selected line
        if (!selectedLines.isEmpty()) {
            m_tableView->scrollTo(m_model->index(m_model->mapFromSourceRow(selectedLines.first()), 0));
        }
    }
}

QSet<QString> SyslogViewer::getUniqueFacilities() const
{
    // Reduce function that safely combines facilities into the result set
    auto reduceFacilities = [](QSet<QString>& result, const QString& facility) {
        if (!facility.isEmpty()) {
            result.insert(facility);
        }
        return result;
    };

    // Process entries and reduce results in parallel
    return QtConcurrent::blockingMappedReduced<QSet<QString>>(
        m_entries,
        // Map function to extract facilities
        [this](const LogEntry& entry) {
            SyslogEntry syslogEntry(entry);
            return SyslogEntry::facilityToString(syslogEntry.facility);
        },
        // Reduce function to combine results
        reduceFacilities,
        // Use parallel reduction
        QtConcurrent::ReduceOption::UnorderedReduce
    );
}

bool SyslogViewer::matchesFilter(const SyslogEntry& entry) const
{
    // Check severity filter
    if (!m_severityFilters.value(entry.severity, true)) {
        return false;
    }

    // Check facility filters
    if (!m_selectedFacilities.isEmpty() && 
        !m_selectedFacilities.contains(SyslogEntry::facilityToString(entry.facility))) {
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
