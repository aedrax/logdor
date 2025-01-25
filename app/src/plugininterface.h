#ifndef PLUGININTERFACE_H
#define PLUGININTERFACE_H

#include <QString>
#include <QWidget>
#include <QtPlugin>

class PluginInterface {
public:
    virtual ~PluginInterface() = default;

    // Get the name of the plugin
    virtual QString name() const = 0;

    // Get the widget provided by the plugin
    virtual QWidget* widget() = 0;

    // Load content into the plugin's widget
    virtual bool loadContent(const QByteArray& content) = 0;

    // Apply a filter query to the content
    virtual void applyFilter(const QString& query) = 0;
};

// Define the plugin interface ID
#define PluginInterface_iid "com.logdor.PluginInterface/1.0"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)

#endif // PLUGININTERFACE_H
