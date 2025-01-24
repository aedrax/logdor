#ifndef PLAINTEXTVIEWER_H
#define PLAINTEXTVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QListWidget>
#include <QString>
#include <QtPlugin>

class PlainTextViewer : public QObject, public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    PlainTextViewer();
    ~PlainTextViewer();

    // PluginInterface implementation
    QString name() const override { return "Plain Text Viewer"; }
    QWidget* widget() override { return m_listWidget; }
    bool loadContent(const QByteArray& content) override;
    void applyFilter(const QString& query) override;

private:
    QListWidget* m_listWidget;
    QByteArray m_originalContent;  // Store original content for filtering
};

#endif // PLAINTEXTVIEWER_H
