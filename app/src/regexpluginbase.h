#ifndef REGEXPLUGINBASE_H
#define REGEXPLUGINBASE_H

#include <QObject>
#include <QWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <QStandardPaths>
#include <QDir>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "plugininterface.h"
#include "databasemanager.h"
#include "regextablemodel.h"
#include "searchtokenizer.h"

template<typename EntryType>
struct ParsedEntry {
    int id;
    int sourceLineNumber;
    EntryType data;
    QDateTime lastModified;
    
    static ParsedEntry<EntryType> fromLogEntry(const LogEntry& entry, int lineNumber) {
        ParsedEntry<EntryType> parsed;
        parsed.id = 0; // Will be set by database
        parsed.sourceLineNumber = lineNumber;
        parsed.data = EntryType::fromLogEntry(entry);
        parsed.lastModified = QDateTime::currentDateTime();
        return parsed;
    }
    
    QList<ParsedField> toFields() const {
        QList<ParsedField> fields;
        
        // Add source line number field
        ParsedField lineField;
        lineField.name = "source_line_number";
        lineField.value = sourceLineNumber;
        lineField.type = DataType::Integer;
        fields.append(lineField);
        
        // Add last modified field
        ParsedField modifiedField;
        modifiedField.name = "last_modified";
        modifiedField.value = lastModified;
        modifiedField.type = DataType::DateTime;
        fields.append(modifiedField);
        
        // Add entry-specific fields (implemented by derived classes)
        return fields;
    }
};

template<typename EntryType>
class RegexPluginBase : public PluginInterface
{
public:
    explicit RegexPluginBase(QObject* parent = nullptr);
    ~RegexPluginBase() override;

    // PluginInterface implementation (final)
    QWidget* widget() override final;
    bool setLogs(const QList<LogEntry>& logs) override final;
    void setFilter(const FilterOptions& options) override final;
    QSet<int> filteredLines() const override final;
    void synchronizeFilteredLines(const QSet<int>& lines) override final;
    void onPluginEvent(PluginEvent event, const QVariant& data) override final;

    // Pure virtual functions that derived classes must implement
    virtual EntryType parseEntry(const LogEntry& entry) = 0;
    virtual QList<ColumnDefinition> getColumnDefinitions() = 0;
    virtual QString getDatabaseSchema() = 0;
    virtual QList<ParsedField> entryToFields(const EntryType& entry) = 0;

    // Optional virtual functions with default implementations
    virtual QString getDatabasePath();
    virtual QString getTableName() { return "parsed_entries"; }
    virtual int getDatabaseSchemaVersion() { return 1; }
    virtual QStringList getSchemaMigrationCommands(int fromVersion, int toVersion);
    virtual SearchTokenizer* createTokenizer();
    virtual void customizeTableView(QTableView* tableView);

protected:
    // Database access for derived classes
    DatabaseManager* database() { return m_database; }
    RegexTableModel* tableModel() { return m_model; }
    QTableView* tableView() { return m_tableView; }
    
    // Parsing status
    ParseStatus parseStatus() const { return m_parseStatus; }
    int totalEntries() const { return m_totalEntries; }
    int parsedEntries() const { return m_parsedEntries; }

signals:
    void parseProgressChanged(int current, int total);
    void parseStatusChanged(ParseStatus status);
    void parseCompleted(bool success);

private slots:
    void onParseFinished();
    void onDatabaseStatusChanged(DatabaseStatus status);
    void onTableModelError(const QString& error);
    void onSelectionChanged();

private:
    // Initialization
    bool initializeDatabase();
    bool createOrMigrateSchema();
    void setupTableModel();
    void setupWidget();
    
    // Background parsing
    void startParsing(const QList<LogEntry>& logs);
    void stopParsing();
    QList<ParsedEntry<EntryType>> parseLogEntries(const QList<LogEntry>& logs);
    bool storeEntries(const QList<ParsedEntry<EntryType>>& entries);
    bool updateSearchIndex(const QList<ParsedEntry<EntryType>>& entries);
    
    // Widget components
    QWidget* m_widget;
    QVBoxLayout* m_layout;
    QTableView* m_tableView;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    
    // Data management
    DatabaseManager* m_database;
    RegexTableModel* m_model;
    SearchTokenizer* m_tokenizer;
    
    // Parsing state
    ParseStatus m_parseStatus;
    int m_totalEntries;
    int m_parsedEntries;
    QFutureWatcher<QList<ParsedEntry<EntryType>>>* m_parseWatcher;
    QTimer* m_progressTimer;
    
    // Current data
    QList<LogEntry> m_currentLogs;
    bool m_needsReparsing;
    
    static const QString DATABASE_EXTENSION;
    static const int PROGRESS_UPDATE_INTERVAL = 100; // ms
    static const int PARSE_BATCH_SIZE = 1000;
};

// Template implementation

template<typename EntryType>
const QString RegexPluginBase<EntryType>::DATABASE_EXTENSION = ".logdor.db";

template<typename EntryType>
RegexPluginBase<EntryType>::RegexPluginBase(QObject* parent)
    : PluginInterface(parent)
    , m_widget(nullptr)
    , m_layout(nullptr)
    , m_tableView(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_database(nullptr)
    , m_model(nullptr)
    , m_tokenizer(nullptr)
    , m_parseStatus(ParseStatus::NotParsed)
    , m_totalEntries(0)
    , m_parsedEntries(0)
    , m_parseWatcher(nullptr)
    , m_progressTimer(nullptr)
    , m_needsReparsing(false)
{
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(PROGRESS_UPDATE_INTERVAL);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        emit parseProgressChanged(m_parsedEntries, m_totalEntries);
    });
    
    // Create tokenizer
    m_tokenizer = createTokenizer();
}

template<typename EntryType>
RegexPluginBase<EntryType>::~RegexPluginBase()
{
    stopParsing();
    
    if (m_database) {
        m_database->closeDatabase();
        delete m_database;
    }
    
    if (m_tokenizer) {
        delete m_tokenizer;
    }
}

template<typename EntryType>
QWidget* RegexPluginBase<EntryType>::widget()
{
    if (!m_widget) {
        setupWidget();
        
        if (!initializeDatabase()) {
            m_statusLabel->setText("Failed to initialize database");
            return m_widget;
        }
        
        setupTableModel();
    }
    
    return m_widget;
}

template<typename EntryType>
bool RegexPluginBase<EntryType>::setLogs(const QList<LogEntry>& logs)
{
    if (logs.isEmpty()) {
        return true;
    }
    
    m_currentLogs = logs;
    m_needsReparsing = true;
    
    // Start parsing in background
    startParsing(logs);
    
    return true;
}

template<typename EntryType>
void RegexPluginBase<EntryType>::setFilter(const FilterOptions& options)
{
    if (m_model) {
        m_model->applyFilter(options);
    }
}

template<typename EntryType>
QSet<int> RegexPluginBase<EntryType>::filteredLines() const
{
    if (!m_model) {
        return QSet<int>();
    }
    
    QSet<int> filtered;
    QList<int> sourceRows = m_model->getSelectedSourceRows();
    for (int row : sourceRows) {
        filtered.insert(row);
    }
    
    return filtered;
}

template<typename EntryType>
void RegexPluginBase<EntryType>::synchronizeFilteredLines(const QSet<int>& lines)
{
    if (m_model) {
        QList<int> linesList(lines.begin(), lines.end());
        m_model->setSelectedSourceRows(linesList);
    }
}

template<typename EntryType>
void RegexPluginBase<EntryType>::onPluginEvent(PluginEvent event, const QVariant& data)
{
    switch (event) {
    case PluginEvent::LinesSelected:
        {
            QList<int> selectedLines = data.value<QList<int>>();
            QSet<int> lineSet(selectedLines.begin(), selectedLines.end());
            synchronizeFilteredLines(lineSet);
        }
        break;
        
    case PluginEvent::LinesFiltered:
        {
            QSet<int> filteredLines = data.value<QSet<int>>();
            synchronizeFilteredLines(filteredLines);
        }
        break;
        
    default:
        // Handle custom events in derived classes if needed
        break;
    }
}

template<typename EntryType>
QString RegexPluginBase<EntryType>::getDatabasePath()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir);
    
    QString filename = QString("%1_%2%3")
                          .arg(name().toLower())
                          .arg(QDateTime::currentDateTime().toString("yyyyMMdd"))
                          .arg(DATABASE_EXTENSION);
    
    return QDir(dataDir).absoluteFilePath(filename);
}

template<typename EntryType>
QStringList RegexPluginBase<EntryType>::getSchemaMigrationCommands(int fromVersion, int toVersion)
{
    Q_UNUSED(fromVersion);
    Q_UNUSED(toVersion);
    return QStringList(); // No migrations by default
}

template<typename EntryType>
SearchTokenizer* RegexPluginBase<EntryType>::createTokenizer()
{
    return new SearchTokenizer();
}

template<typename EntryType>
void RegexPluginBase<EntryType>::customizeTableView(QTableView* tableView)
{
    Q_UNUSED(tableView);
    // Default implementation does nothing
    // Derived classes can override to customize appearance
}

template<typename EntryType>
bool RegexPluginBase<EntryType>::initializeDatabase()
{
    QString dbPath = getDatabasePath();
    m_database = new DatabaseManager(dbPath, this);
    
    connect(m_database, &DatabaseManager::statusChanged,
            this, &RegexPluginBase<EntryType>::onDatabaseStatusChanged);
    
    if (!createOrMigrateSchema()) {
        return false;
    }
    
    return m_database->status() == DatabaseStatus::Ready;
}

template<typename EntryType>
bool RegexPluginBase<EntryType>::createOrMigrateSchema()
{
    QString schema = getDatabaseSchema();
    if (!m_database->createDatabase(schema)) {
        return false;
    }
    
    // Check for schema migrations
    int currentVersion = m_database->getSchemaVersion();
    int targetVersion = getDatabaseSchemaVersion();
    
    if (currentVersion < targetVersion) {
        QStringList migrationCommands = getSchemaMigrationCommands(currentVersion, targetVersion);
        if (!migrationCommands.isEmpty()) {
            return m_database->migrateSchema(currentVersion, targetVersion, migrationCommands);
        }
    }
    
    // Set initial schema version if not set
    if (currentVersion == 0) {
        return m_database->updateSchemaVersion(targetVersion);
    }
    
    return true;
}

template<typename EntryType>
void RegexPluginBase<EntryType>::setupTableModel()
{
    if (!m_database) {
        return;
    }
    
    m_model = new RegexTableModel(m_database, this);
    m_model->setTableName(getTableName());
    m_model->setColumnDefinitions(getColumnDefinitions());
    
    connect(m_model, &RegexTableModel::errorOccurred,
            this, &RegexPluginBase<EntryType>::onTableModelError);
    
    if (m_tableView) {
        m_tableView->setModel(m_model);
        connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &RegexPluginBase<EntryType>::onSelectionChanged);
    }
}

template<typename EntryType>
void RegexPluginBase<EntryType>::setupWidget()
{
    m_widget = new QWidget();
    m_layout = new QVBoxLayout(m_widget);
    
    // Status label
    m_statusLabel = new QLabel("Initializing...");
    m_layout->addWidget(m_statusLabel);
    
    // Progress bar
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_layout->addWidget(m_progressBar);
    
    // Table view
    m_tableView = new QTableView();
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(true);
    
    customizeTableView(m_tableView);
    
    m_layout->addWidget(m_tableView);
    
    m_widget->setLayout(m_layout);
}

template<typename EntryType>
void RegexPluginBase<EntryType>::startParsing(const QList<LogEntry>& logs)
{
    if (m_parseStatus == ParseStatus::Parsing) {
        stopParsing();
    }
    
    m_parseStatus = ParseStatus::Parsing;
    m_totalEntries = logs.size();
    m_parsedEntries = 0;
    
    emit parseStatusChanged(m_parseStatus);
    
    // Show progress UI
    m_statusLabel->setText(QString("Parsing %1 log entries...").arg(m_totalEntries));
    m_progressBar->setRange(0, m_totalEntries);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_progressTimer->start();
    
    // Start background parsing
    m_parseWatcher = new QFutureWatcher<QList<ParsedEntry<EntryType>>>(this);
    connect(m_parseWatcher, &QFutureWatcher<QList<ParsedEntry<EntryType>>>::finished,
            this, &RegexPluginBase<EntryType>::onParseFinished);
    
    QFuture<QList<ParsedEntry<EntryType>>> future = 
        QtConcurrent::run([this, logs]() { return parseLogEntries(logs); });
    
    m_parseWatcher->setFuture(future);
}

template<typename EntryType>
void RegexPluginBase<EntryType>::stopParsing()
{
    if (m_parseWatcher) {
        m_parseWatcher->cancel();
        m_parseWatcher->waitForFinished();
        delete m_parseWatcher;
        m_parseWatcher = nullptr;
    }
    
    m_progressTimer->stop();
}

template<typename EntryType>
QList<ParsedEntry<EntryType>> RegexPluginBase<EntryType>::parseLogEntries(const QList<LogEntry>& logs)
{
    QList<ParsedEntry<EntryType>> entries;
    entries.reserve(logs.size());
    
    for (int i = 0; i < logs.size(); ++i) {
        if (m_parseWatcher && m_parseWatcher->isCanceled()) {
            break;
        }
        
        try {
            EntryType parsed = parseEntry(logs[i]);
            ParsedEntry<EntryType> entry = ParsedEntry<EntryType>::fromLogEntry(logs[i], i);
            entry.data = parsed;
            entries.append(entry);
            
            m_parsedEntries = i + 1;
            
            // Batch progress updates
            if (i % PARSE_BATCH_SIZE == 0) {
                // Progress updates happen via timer to avoid UI thread overload
            }
            
        } catch (const std::exception& e) {
            qWarning() << "Failed to parse entry" << i << ":" << e.what();
            // Continue parsing other entries
        }
    }
    
    return entries;
}

template<typename EntryType>
bool RegexPluginBase<EntryType>::storeEntries(const QList<ParsedEntry<EntryType>>& entries)
{
    if (!m_database || entries.isEmpty()) {
        return false;
    }
    
    // Clear existing data
    if (!m_database->executeNonQuery(QString("DELETE FROM %1").arg(getTableName()))) {
        return false;
    }
    
    // Prepare bulk insert
    QList<ColumnDefinition> columns = getColumnDefinitions();
    QStringList columnNames;
    for (const ColumnDefinition& column : columns) {
        columnNames.append(column.name);
    }
    
    if (!m_database->prepareBulkInsert(getTableName(), columnNames)) {
        return false;
    }
    
    // Insert entries
    bool success = true;
    for (const ParsedEntry<EntryType>& entry : entries) {
        QList<ParsedField> fields = entryToFields(entry.data);
        
        // Add common fields
        fields.append({"source_line_number", entry.sourceLineNumber, DataType::Integer});
        fields.append({"last_modified", entry.lastModified, DataType::DateTime});
        
        QVariantList values;
        for (const ParsedField& field : fields) {
            values.append(field.value);
        }
        
        if (!m_database->executeBulkInsert(values)) {
            success = false;
            break;
        }
    }
    
    m_database->finalizeBulkInsert();
    return success;
}

template<typename EntryType>
bool RegexPluginBase<EntryType>::updateSearchIndex(const QList<ParsedEntry<EntryType>>& entries)
{
    if (!m_database || !m_tokenizer || entries.isEmpty()) {
        return false;
    }
    
    // Clear existing search tokens
    if (!m_database->executeNonQuery("DELETE FROM search_tokens")) {
        return false;
    }
    
    bool success = true;
    for (int i = 0; i < entries.size(); ++i) {
        const ParsedEntry<EntryType>& entry = entries[i];
        QList<ParsedField> fields = entryToFields(entry.data);
        
        for (const ParsedField& field : fields) {
            QString content = field.value.toString();
            if (!content.isEmpty()) {
                if (!m_database->updateSearchIndex(i + 1, field.name, content)) {
                    success = false;
                    break;
                }
            }
        }
        
        if (!success) {
            break;
        }
    }
    
    return success;
}

template<typename EntryType>
void RegexPluginBase<EntryType>::onParseFinished()
{
    if (!m_parseWatcher) {
        return;
    }
    
    m_progressTimer->stop();
    
    bool success = false;
    
    if (!m_parseWatcher->isCanceled()) {
        QList<ParsedEntry<EntryType>> entries = m_parseWatcher->result();
        
        if (!entries.isEmpty()) {
            // Store entries in database
            if (storeEntries(entries)) {
                // Update search index
                if (updateSearchIndex(entries)) {
                    success = true;
                    m_parseStatus = ParseStatus::Complete;
                    m_statusLabel->setText(QString("Parsed %1 entries successfully").arg(entries.size()));
                    
                    // Refresh table model
                    if (m_model) {
                        m_model->refresh();
                    }
                } else {
                    m_parseStatus = ParseStatus::Failed;
                    m_statusLabel->setText("Failed to update search index");
                }
            } else {
                m_parseStatus = ParseStatus::Failed;
                m_statusLabel->setText("Failed to store parsed entries");
            }
        } else {
            m_parseStatus = ParseStatus::Failed;
            m_statusLabel->setText("No entries were successfully parsed");
        }
    } else {
        m_parseStatus = ParseStatus::Failed;
        m_statusLabel->setText("Parsing was cancelled");
    }
    
    m_progressBar->setVisible(false);
    
    delete m_parseWatcher;
    m_parseWatcher = nullptr;
    
    emit parseStatusChanged(m_parseStatus);
    emit parseCompleted(success);
    
    m_needsReparsing = false;
}

template<typename EntryType>
void RegexPluginBase<EntryType>::onDatabaseStatusChanged(DatabaseStatus status)
{
    if (status == DatabaseStatus::Ready) {
        m_statusLabel->setText("Database ready");
        
        if (m_needsReparsing && !m_currentLogs.isEmpty()) {
            startParsing(m_currentLogs);
        }
    } else if (status == DatabaseStatus::Error) {
        m_statusLabel->setText("Database error: " + m_database->lastError());
    }
}

template<typename EntryType>
void RegexPluginBase<EntryType>::onTableModelError(const QString& error)
{
    m_statusLabel->setText("Table error: " + error);
}

template<typename EntryType>
void RegexPluginBase<EntryType>::onSelectionChanged()
{
    if (!m_tableView || !m_model) {
        return;
    }
    
    QModelIndexList selectedIndexes = m_tableView->selectionModel()->selectedRows();
    QList<int> selectedRows;
    
    for (const QModelIndex& index : selectedIndexes) {
        int sourceRow = m_model->mapToSourceRow(index.row());
        if (sourceRow >= 0) {
            selectedRows.append(sourceRow);
        }
    }
    
    if (!selectedRows.isEmpty()) {
        emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(selectedRows));
    }
}

#endif // REGEXPLUGINBASE_H
