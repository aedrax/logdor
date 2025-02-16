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

class PluginInterface {
public:
    virtual ~PluginInterface() = default;

    // Get the name of the plugin
    virtual QString name() const = 0;

    // Get the widget provided by the plugin
    virtual QWidget* widget() = 0;

    // Load content into the plugin's widget
    virtual bool loadContent(const QVector<LogEntry>& content) = 0;

    // Apply filter options to the content
    virtual void applyFilter(const FilterOptions& options) = 0;
};

// Define the plugin interface ID
#define PluginInterface_iid "com.logdor.PluginInterface/1.0"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)

#endif // PLUGININTERFACE_H
