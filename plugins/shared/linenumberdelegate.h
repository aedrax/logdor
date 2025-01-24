#ifndef LINENUMBERDELEGATE_H
#define LINENUMBERDELEGATE_H

#include <QStyledItemDelegate>
#include <QMap>

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

#endif // LINENUMBERDELEGATE_H
