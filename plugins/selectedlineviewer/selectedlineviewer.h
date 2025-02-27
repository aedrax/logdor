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
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer for selected lines of logs."); }
    QWidget* widget() override { return m_textBrowser; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    QTextBrowser* m_textBrowser;
    QList<LogEntry> m_entries;
};

#endif // SELECTEDLINEVIEWER_H
