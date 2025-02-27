#ifndef PLUGININTERFACE_H
#define PLUGININTERFACE_H

#include <QDateTime>
#include <QString>
#include <QWidget>
#include <QtPlugin>

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

    FilterOptions(const QString& q = QString(), int before = 0, int after = 0, Qt::CaseSensitivity cs = Qt::CaseInsensitive)
        : query(q)
        , contextLinesBefore(before)
        , contextLinesAfter(after)
        , caseSensitivity(cs)
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

class PluginInterface : public QObject {
    Q_OBJECT
public:
    // Constructor
    explicit PluginInterface(QObject* parent = nullptr) : QObject(parent) {}

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

    // Load logs into the plugin's widget
    virtual bool setLogs(const QVector<LogEntry>& logs) = 0;

    // Apply filter options to the logs
    virtual void setFilter(const FilterOptions& options) = 0;

    // Metadata about fields
    virtual QList<FieldInfo> availableFields() const = 0;

    // Communicate filtered lines
    virtual QSet<int> filteredLines() const = 0; // Returns indices of filtered out lines
    virtual void synchronizeFilteredLines(const QSet<int>& lines) = 0; // Synchronize with other plugins    

public slots:
    // Handle plugin events
    virtual void onPluginEvent(PluginEvent event, const QVariant& data) = 0;
    
signals:
    // Signal to notify about plugin events
    void pluginEvent(PluginEvent event, const QVariant& data);
};

// Define the plugin interface ID
#define PluginInterface_iid "com.logdor.PluginInterface/1.0"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)

#endif // PLUGININTERFACE_H
