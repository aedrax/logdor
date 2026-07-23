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

    QString name() const override { return tr("Selected Line Viewer"); }
    QString version() const override { return "0.2.0"; }
    QString description() const override { return tr("A viewer for selected lines of logs."); }
    QWidget* widget() override { return m_textBrowser; }

    // Core-source path: line text is read on demand, O(selected lines).
    bool wantsCoreSource() const override { return true; }
    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override;

    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    QTextBrowser* m_textBrowser;
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
};

#endif // SELECTEDLINEVIEWER_H
