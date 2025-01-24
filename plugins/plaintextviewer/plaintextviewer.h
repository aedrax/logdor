#ifndef PLAINTEXTVIEWER_H
#define PLAINTEXTVIEWER_H

#include "../../app/src/plugininterface.h"
#include <QListWidget>
#include <QString>
#include <QtPlugin>
#include <QStyledItemDelegate>

class LineNumberDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LineNumberDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setLineNumber(int row, int number);
    void clearLineNumbers();

private:
    static constexpr int LINE_NUMBER_MARGIN = 50;
    QMap<int, int> m_rowToLineNumber;
};

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
    QVector<QPair<int, QString>> m_lines;  // Store line numbers with content
    LineNumberDelegate* m_lineNumberDelegate;
};

#endif // PLAINTEXTVIEWER_H
