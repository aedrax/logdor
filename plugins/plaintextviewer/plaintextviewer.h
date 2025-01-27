#ifndef PLAINTEXTVIEWER_H
#define PLAINTEXTVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTableWidget>
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
    QString name() const override { return tr("Plain Text Viewer"); }
    QWidget* widget() override { return m_tableWidget; }
    bool loadContent(const QByteArray& content) override;
    void applyFilter(const QString& query, int contextLinesBefore = 0, int contextLinesAfter = 0) override;

private:
    QTableWidget* m_tableWidget;
    QByteArray m_originalContent;  // Store original content for filtering
    QVector<QPair<int, QString>> m_lines;  // Store line numbers with content
    QVector<bool> m_directMatches;  // Tracks which lines directly match the filter
};

#endif // PLAINTEXTVIEWER_H
