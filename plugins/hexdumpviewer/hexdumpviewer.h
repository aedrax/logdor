#ifndef HEXDUMPVIEWER_H
#define HEXDUMPVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTextEdit>
#include <QtPlugin>

class HexDumpViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit HexDumpViewer(QObject* parent = nullptr);
    ~HexDumpViewer();

    QString name() const override { return tr("Hex Dump Viewer"); }
    QString version() const override { return "0.2.0"; }
    QString description() const override { return tr("Hex dump of the selected log lines."); }
    QWidget* widget() override { return m_textEdit; }

    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index) override;

    void setFilter(const FilterOptions& options) override { Q_UNUSED(options) }

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    QString generateHexDump(const QByteArray& data) const;

    QTextEdit* m_textEdit;
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
};

#endif // HEXDUMPVIEWER_H
