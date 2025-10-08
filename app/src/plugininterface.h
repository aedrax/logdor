#ifndef PLUGININTERFACE_H
#define PLUGININTERFACE_H

#include <QDateTime>
#include <QString>
#include <QWidget>
#include <QtPlugin>

// Forward declaration to avoid circular dependency
class PluginDatabaseManager;

struct LogEntry {
    const char* message;
    size_t length;

    LogEntry(const char* msg = nullptr, size_t len = 0)
        : message(msg)
        , length(len)
    {
    }

    QString getMessage() const {
        return QString::fromUtf8(message, static_cast<int>(length));
    }
};

struct FilterOptions {
    QString query;
    int contextLinesBefore = 0;
    int contextLinesAfter = 0;
    Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
    bool invertFilter = false;
    bool inQueryMode = false;
    bool inRegexMode = false;

    FilterOptions(const QString& q = QString(), int before = 0, int after = 0, Qt::CaseSensitivity cs = Qt::CaseInsensitive, bool invert = false, bool queryMode = false, bool regexMode = false)
        : query(q)
        , contextLinesBefore(before)
        , contextLinesAfter(after)
        , caseSensitivity(cs)
        , invertFilter(invert)
        , inQueryMode(queryMode)
        , inRegexMode(regexMode)
    {
    }
};

enum class DataType {
    String,
    Integer,
    DateTime,
    // Add other data types as needed
};

struct FieldInfo {
    QString name;
    DataType type;
    QList<QVariant> possibleValues; // For non-basic types or enums
};

enum class PluginEvent {
    Custom,
    LinesSelected,
    LinesFiltered,
    // Add other events as needed
};

// Using Q_DECL_EXPORT to export the functions in the library (Windows)
// don't need Q_DECL_IMPORT because it's never imported, only loaded at runtime
class Q_DECL_EXPORT PluginInterface : public QObject {
    Q_OBJECT
public:
    // Constructor
    explicit PluginInterface(QObject* parent = nullptr) : QObject(parent), m_enabled(true) {}

    // Virtual destructor for proper cleanup
    virtual ~PluginInterface() = default;

    // Get the name of the plugin
    virtual QString name() const = 0;

    // Get the version of the plugin
    virtual QString version() const = 0;

    // Get the description of the plugin
    virtual QString description() const = 0;

    // Get the widget provided by the plugin
    virtual QWidget* widget() = 0;

    // Enable/disable the plugin
    virtual void setEnabled(bool enabled) { 
        if (m_enabled != enabled) {
            m_enabled = enabled;
            emit enabledChanged(enabled);
        }
    }

    // Check if plugin is enabled
    virtual bool isEnabled() const { return m_enabled; }

    // Load logs into the plugin's widget
    virtual bool setLogs(const QList<LogEntry>& logs) = 0;

    // Apply filter options to the logs
    virtual void setFilter(const FilterOptions& options) = 0;

    // Metadata about fields
    virtual QList<FieldInfo> availableFields() const = 0;

    // Communicate filtered lines
    virtual QSet<int> filteredLines() const = 0; // Returns indices of filtered out lines
    virtual void synchronizeFilteredLines(const QSet<int>& lines) = 0; // Synchronize with other plugins    

    // Database-aware methods (optional, for backward compatibility)
    // Plugins can override these to support database storage
    
    // Check if plugin supports database storage
    virtual bool supportsDatabaseStorage() const { return false; }
    
    // Get database schema definition for this plugin
    // Returns empty list if plugin doesn't support database storage
    virtual QList<FieldInfo> getDatabaseSchema() const { return QList<FieldInfo>(); }
    
    // Parse a log entry into a database record
    // Returns empty list if plugin doesn't support database storage
    virtual QVariantList parseToDatabaseRecord(const LogEntry& entry, int lineNumber) const { 
        Q_UNUSED(entry)
        Q_UNUSED(lineNumber)
        return QVariantList(); 
    }
    
    // Database manager integration (dependency injection)
    // Set the database manager for this plugin instance
    // Returns true if successfully set, false if plugin doesn't support database storage
    virtual bool setDatabaseManager(PluginDatabaseManager* manager) { 
        if (supportsDatabaseStorage()) {
            m_databaseManager = manager;
            return true;
        }
        return false; 
    }
    
    // Plugin lifecycle methods for database operations
    // Called when database is ready for this plugin
    virtual bool initializeDatabase() { return true; }
    
    // Called when plugin should clean up database resources
    virtual void cleanupDatabase() { }
    
    // Error handling for database operations
    // Called when a database operation fails
    virtual void onDatabaseError(const QString& error) { 
        Q_UNUSED(error)
        // Default implementation: emit a generic plugin event
        emit const_cast<PluginInterface*>(this)->pluginEvent(PluginEvent::Custom, 
            QVariantMap{{"type", "database_error"}, {"message", error}});
    }

public slots:
    // Handle plugin events
    virtual void onPluginEvent(PluginEvent event, const QVariant& data) = 0;
    
signals:
    // Signal to notify about plugin events
    void pluginEvent(PluginEvent event, const QVariant& data);
    
    // Signal emitted when plugin enabled state changes
    void enabledChanged(bool enabled);

protected:
    bool m_enabled;
    PluginDatabaseManager* m_databaseManager = nullptr;
    
    // Protected getter for database manager (for derived classes)
    PluginDatabaseManager* databaseManager() const { return m_databaseManager; }
};

// Define the plugin interface ID
#define PluginInterface_iid "com.logdor.PluginInterface/1.0"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)

#endif // PLUGININTERFACE_H
