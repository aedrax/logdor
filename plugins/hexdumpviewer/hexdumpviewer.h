#ifndef HEXDUMPVIEWER_H
#define HEXDUMPVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTextEdit>
#include <QString>
#include <QtPlugin>

class HexDumpViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit HexDumpViewer(QObject* parent = nullptr);
    ~HexDumpViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Hex Dump Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("Shows a hexadecimal dump of selected lines."); }
    QWidget* widget() override { return m_textEdit; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private:
    QString generateHexDump(const QByteArray& data) const;
    QTextEdit* m_textEdit;
    QList<LogEntry> m_entries;
};

#endif // HEXDUMPVIEWER_H
