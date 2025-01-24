#include "linenumberdelegate.h"
#include <QPainter>
#include <QApplication>

LineNumberDelegate::LineNumberDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void LineNumberDelegate::setLineNumber(int row, int number)
{
    m_rowToLineNumber[row] = number;
}

void LineNumberDelegate::clearLineNumbers()
{
    m_rowToLineNumber.clear();
}

void LineNumberDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // Draw the line number
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Use a slightly different background for the line number area
    QColor lineNumberBg = option.palette.color(QPalette::Base).darker(103);
    painter->fillRect(option.rect.left(), option.rect.top(), LINE_NUMBER_MARGIN, option.rect.height(), lineNumberBg);
    
    // Draw a subtle separator line
    painter->setPen(option.palette.color(QPalette::Base).darker(120));
    painter->drawLine(LINE_NUMBER_MARGIN - 1, option.rect.top(), LINE_NUMBER_MARGIN - 1, option.rect.bottom());
    
    // Draw the line number text
    painter->setPen(option.palette.color(QPalette::Text));
    QFont font = QApplication::font();
    painter->setFont(font);
    
    // Use the mapped line number if available, otherwise use row + 1
    int lineNumber = m_rowToLineNumber.value(index.row(), index.row() + 1);
    painter->drawText(
        QRect(0, option.rect.top(), LINE_NUMBER_MARGIN - 5, option.rect.height()),
        Qt::AlignRight | Qt::AlignVCenter,
        QString::number(lineNumber)
    );
    painter->restore();
    
    // Draw the actual content with an offset
    QStyleOptionViewItem contentOption = option;
    contentOption.rect.setLeft(option.rect.left() + LINE_NUMBER_MARGIN);
    QStyledItemDelegate::paint(painter, contentOption, index);
}

QSize LineNumberDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setWidth(size.width() + LINE_NUMBER_MARGIN);
    return size;
}
