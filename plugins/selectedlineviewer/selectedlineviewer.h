#ifndef SELECTEDLINEVIEWER_H
#define SELECTEDLINEVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTextBrowser>
#include <QString>
#include <QtPlugin>

class SelectedLineViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit SelectedLineViewer(QObject* parent = nullptr);
    ~SelectedLineViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Selected Line Viewer"); }
    QWidget* widget() override { return m_textBrowser; }
    bool loadContent(const QVector<LogEntry>& content) override;
    void applyFilter(const FilterOptions& options) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    QTextBrowser* m_textBrowser;
    QVector<LogEntry> m_entries;
};

#endif // SELECTEDLINEVIEWER_H
