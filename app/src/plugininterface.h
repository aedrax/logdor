#ifndef PLUGININTERFACE_H
#define PLUGININTERFACE_H

#include <QString>
#include <QWidget>
#include <QtPlugin>

struct FilterOptions {
    QString query;
    int contextLinesBefore = 0;
    int contextLinesAfter = 0;

    FilterOptions(const QString& q = QString(), int before = 0, int after = 0)
        : query(q)
        , contextLinesBefore(before)
        , contextLinesAfter(after)
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
    virtual bool loadContent(const QByteArray& content) = 0;

    // Apply filter options to the content
    virtual void applyFilter(const FilterOptions& options) = 0;
};

// Define the plugin interface ID
#define PluginInterface_iid "com.logdor.PluginInterface/1.0"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)

#endif // PLUGININTERFACE_H
