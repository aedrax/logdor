#ifndef PLAINTEXTVIEWER_H
#define PLAINTEXTVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QTableWidget>
#include <QString>
#include <QtPlugin>

struct LineInfo {
    int number;
    QString content;
    bool isDirectMatch;

    LineInfo(int num, const QString& cont)
        : number(num)
        , content(cont)
        , isDirectMatch(false)
    {
    }
};

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
    void applyFilter(const FilterOptions& options) override;

private:
    QTableWidget* m_tableWidget;
    QByteArray m_originalContent; // Store original content for filtering
    QVector<LineInfo> m_lines; // Store all line information
};

#endif // PLAINTEXTVIEWER_H
