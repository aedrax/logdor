#include "log4jviewer.h"
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QLabel>

Log4jViewer::Log4jViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget)
    , m_layout(new QVBoxLayout(m_container))
    , m_toolbar(new QToolBar)
    , m_tableView(new QTableView)
    , m_model(new Log4jTableModel(this))
    , m_loggerComboBox(new QComboBox)
    , m_scrollArea(new QScrollArea)
    , m_loggersContainer(new QFrame)
    , m_loggersLayout(new QHBoxLayout(m_loggersContainer))
    , m_patternInput(new QLineEdit)
    , m_currentPattern("%d{yyyy-MM-dd HH:mm:ss} %-5p [%t] %c{1} - %m%n")
{
    setupUi();
}

Log4jViewer::~Log4jViewer()
{
    delete m_container;
}

void Log4jViewer::setupUi()
{
    // Pattern input setup
    QHBoxLayout* patternLayout = new QHBoxLayout;
    QLabel* patternLabel = new QLabel(tr("Log4j Pattern:"));
    m_patternInput->setText(m_currentPattern);
    m_patternInput->setToolTip(tr("Enter Log4j pattern layout (e.g. %d{ISO8601} [%t] %p %c - %m%n)"));
    patternLayout->addWidget(patternLabel);
    patternLayout->addWidget(m_patternInput);
    connect(m_patternInput, &QLineEdit::editingFinished, this, &Log4jViewer::onPatternChanged);
    m_layout->addLayout(patternLayout);

    // Level filter buttons
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    
    auto addLevelAction = [this](Log4jEntry::Level level, const QString& text, const QColor& color) {
        QAction* action = new QAction(text, this);
        action->setCheckable(true);
        action->setChecked(true);
        
        // Create a colored icon
        QPixmap pixmap(16, 16);
        pixmap.fill(color);
        action->setIcon(QIcon(pixmap));
        
        m_levelActions[level] = action;
        m_levelFilters[level] = true;
        
        connect(action, &QAction::toggled, this, [this, level](bool checked) {
            toggleLevel(level, checked);
        });
        
        m_toolbar->addAction(action);
    };

    addLevelAction(Log4jEntry::Level::Trace, tr("TRACE"), Log4jEntry::levelColor(Log4jEntry::Level::Trace));
    addLevelAction(Log4jEntry::Level::Debug, tr("DEBUG"), Log4jEntry::levelColor(Log4jEntry::Level::Debug));
    addLevelAction(Log4jEntry::Level::Info, tr("INFO"), Log4jEntry::levelColor(Log4jEntry::Level::Info));
    addLevelAction(Log4jEntry::Level::Warn, tr("WARN"), Log4jEntry::levelColor(Log4jEntry::Level::Warn));
    addLevelAction(Log4jEntry::Level::Error, tr("ERROR"), Log4jEntry::levelColor(Log4jEntry::Level::Error));
    addLevelAction(Log4jEntry::Level::Fatal, tr("FATAL"), Log4jEntry::levelColor(Log4jEntry::Level::Fatal));

    m_toolbar->addSeparator();

    // Logger filter
    QLabel* loggerLabel = new QLabel(tr("Logger:"));
    m_toolbar->addWidget(loggerLabel);
    m_loggerComboBox->setMinimumWidth(200);
    m_toolbar->addWidget(m_loggerComboBox);
    connect(m_loggerComboBox, &QComboBox::currentTextChanged, this, &Log4jViewer::onLoggerFilterChanged);

    m_layout->addWidget(m_toolbar);

    // Logger tags scroll area
    m_scrollArea->setWidget(m_loggersContainer);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setMaximumHeight(50);
    m_loggersContainer->setLayout(m_loggersLayout);
    m_loggersLayout->setAlignment(Qt::AlignLeft);
    m_loggersLayout->setContentsMargins(5, 5, 5, 5);
    m_loggersLayout->setSpacing(5);
    m_layout->addWidget(m_scrollArea);

    // Table setup
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(Log4jTableModel::Message, QHeaderView::Stretch);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setSortingEnabled(true);
    m_layout->addWidget(m_tableView);

    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &Log4jViewer::onSelectionChanged);
}

bool Log4jViewer::setLogs(const QList<LogEntry>& content)
{
    m_entries = content;
    QList<Log4jEntry> log4jEntries;
    log4jEntries.reserve(content.size());

    for (const auto& entry : content) {
        log4jEntries.append(Log4jEntry(entry, m_currentPattern));
    }

    m_model->setEntries(log4jEntries);
    
    // Update logger filter
    m_loggerComboBox->clear();
    m_loggerComboBox->addItem(tr("All Loggers"));
    QSet<QString> loggers = m_model->getUniqueLoggers();
    for (const QString& logger : loggers) {
        m_loggerComboBox->addItem(logger);
    }

    return true;
}

void Log4jViewer::setFilter(const FilterOptions& options)
{
    m_filterOptions = options;
    updateVisibleRows();
}

QList<FieldInfo> Log4jViewer::availableFields() const
{
    return {
        {tr("Timestamp"), DataType::DateTime},
        {tr("Level"), DataType::String},
        {tr("Thread"), DataType::String},
        {tr("Logger"), DataType::String},
        {tr("Message"), DataType::String},
        {tr("MDC"), DataType::String}
    };
}

QSet<int> Log4jViewer::filteredLines() const
{
    QSet<int> lines;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (!m_tableView->isRowHidden(i)) {
            lines.insert(i);
        }
    }
    return lines;
}

void Log4jViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        m_tableView->setRowHidden(i, !lines.contains(i));
    }
}

void Log4jViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    // Handle plugin events if needed
}

void Log4jViewer::toggleLevel(Log4jEntry::Level level, bool enabled)
{
    m_levelFilters[level] = enabled;
    updateVisibleRows();
}

void Log4jViewer::updateVisibleRows()
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const Log4jEntry& entry = m_model->entries().at(i);
        bool visible = matchesFilter(entry);
        m_tableView->setRowHidden(i, !visible);
    }
}

void Log4jViewer::handleSort(int column, Qt::SortOrder order)
{
    m_model->sort(column, order);
}

void Log4jViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(selected);
    Q_UNUSED(deselected);
    // Handle selection changes if needed
}

void Log4jViewer::onPatternChanged()
{
    QString newPattern = m_patternInput->text();
    if (newPattern != m_currentPattern) {
        m_currentPattern = newPattern;
        setLogs(m_entries); // Reparse with new pattern
    }
}

void Log4jViewer::onLoggerFilterChanged(const QString& logger)
{
    m_selectedLoggers.clear();
    if (logger != tr("All Loggers")) {
        m_selectedLoggers.insert(logger);
    }
    updateVisibleRows();
}

bool Log4jViewer::matchesFilter(const Log4jEntry& entry) const
{
    // Check level filter
    if (!m_levelFilters.value(entry.level, true)) {
        return false;
    }

    // Check logger filter
    if (!m_selectedLoggers.isEmpty() && !m_selectedLoggers.contains(entry.logger)) {
        return false;
    }

    // Check text filter from FilterOptions
    if (!m_filterOptions.query.isEmpty()) {
        QString text = entry.message.toLower();
        if (!text.contains(m_filterOptions.query.toLower())) {
            return false;
        }
    }

    return true;
}

QSet<QString> Log4jViewer::getUniqueLoggers() const
{
    return m_model->getUniqueLoggers();
}
