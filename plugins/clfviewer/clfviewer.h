#ifndef CLFVIEWER_H
#define CLFVIEWER_H

#include "../../app/src/logviewerwidget.h"
#include "../../app/src/plugininterface.h"

#include <QtPlugin>

class CLFViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit CLFViewer(QObject* parent = nullptr);
    ~CLFViewer();

    QString name() const override { return tr("Common Log Format Viewer"); }
    QString version() const override { return "0.2.0"; }
    QString description() const override { return tr("A viewer for Common Log Format (CLF) logs."); }
    QWidget* widget() override { return m_viewer; }

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
    LogViewerWidget* m_viewer;
};

#endif // CLFVIEWER_H
