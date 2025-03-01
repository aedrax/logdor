#include "chessboardwidget.h"
#include <QPainter>
#include <QResizeEvent>

ChessboardWidget::ChessboardWidget(QWidget* parent)
    : QWidget(parent)
    , m_lightSquareColor(Qt::white)
    , m_darkSquareColor(QColor(118, 150, 86))  // Chess.com green
{
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    resetBoard();
}

QSize ChessboardWidget::minimumSizeHint() const
{
    return QSize(200, 200);
}

QSize ChessboardWidget::sizeHint() const
{
    return QSize(400, 400);
}

void ChessboardWidget::resetBoard()
{
    // Standard chess starting position in FEN
    setPosition("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void ChessboardWidget::setPosition(const QString& fen)
{
    m_currentFen = fen;
    m_pieces.clear();
    parseFen(fen);
    update();
}

void ChessboardWidget::parseFen(const QString& fen)
{
    // Split FEN into parts
    QStringList parts = fen.split(' ');
    if (parts.isEmpty()) return;
    
    // Parse piece placement
    QStringList rows = parts[0].split('/');
    if (rows.size() != 8) return;
    
    for (int row = 0; row < 8; ++row) {
        int col = 0;
        for (QChar c : rows[row]) {
            if (c.isDigit()) {
                col += c.digitValue();
            } else {
                QString piece = c.isUpper() ? "w" : "b";
                piece += c.toUpper();
                m_pieces[squareToAlgebraic(row, col)] = piece;
                col++;
            }
        }
    }
}

void ChessboardWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    drawBoard(painter);
    drawPieces(painter);
}

void ChessboardWidget::drawBoard(QPainter& painter)
{
    // Draw squares
    for (int row = 0; row < BOARD_SIZE; ++row) {
        for (int col = 0; col < BOARD_SIZE; ++col) {
            QRect rect = squareRect(row, col);
            painter.fillRect(rect, (row + col) % 2 ? m_darkSquareColor : m_lightSquareColor);
        }
    }
    
    // Draw coordinates
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);
    painter.setPen(Qt::black);
    
    // Draw rank numbers (1-8)
    for (int row = 0; row < BOARD_SIZE; ++row) {
        QRect rect = squareRect(row, 0);
        QRect rankRect(rect.left() - MARGIN, rect.top(), MARGIN - 5, rect.height());
        painter.drawText(rankRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(8 - row));
    }
    
    // Draw file letters (a-h)
    for (int col = 0; col < BOARD_SIZE; ++col) {
        QRect rect = squareRect(BOARD_SIZE - 1, col);
        QRect fileRect(rect.left(), rect.bottom() + 5, rect.width(), MARGIN - 5);
        painter.drawText(fileRect, Qt::AlignHCenter | Qt::AlignTop, QChar('a' + col));
    }
}

void ChessboardWidget::drawPieces(QPainter& painter)
{
    const QMap<QString, QChar> unicodePieces = {
        {"wK", QChar(0x2654)}, {"wQ", QChar(0x2655)}, {"wR", QChar(0x2656)}, 
        {"wB", QChar(0x2657)}, {"wN", QChar(0x2658)}, {"wP", QChar(0x2659)},
        {"bK", QChar(0x265A)}, {"bQ", QChar(0x265B)}, {"bR", QChar(0x265C)},
        {"bB", QChar(0x265D)}, {"bN", QChar(0x265E)}, {"bP", QChar(0x265F)}
    };
    
    QFont font = painter.font();
    font.setPointSize(squareRect(0, 0).height() * 0.7);
    painter.setFont(font);
    
    for (int row = 0; row < BOARD_SIZE; ++row) {
        for (int col = 0; col < BOARD_SIZE; ++col) {
            QString square = squareToAlgebraic(row, col);
            if (m_pieces.contains(square)) {
                QString piece = m_pieces[square];
                QChar unicode = unicodePieces.value(piece);
                
                painter.setPen(piece.startsWith('w') ? Qt::black : Qt::black);
                QRect rect = squareRect(row, col);
                painter.drawText(rect, Qt::AlignCenter, unicode);
            }
        }
    }
}

QRect ChessboardWidget::squareRect(int row, int col) const
{
    int availWidth = width() - 2 * MARGIN;
    int availHeight = height() - 2 * MARGIN;
    int squareSize = qMin(availWidth, availHeight) / BOARD_SIZE;
    
    // Center the board in the available space
    int xOffset = MARGIN + (availWidth - squareSize * BOARD_SIZE) / 2;
    int yOffset = MARGIN + (availHeight - squareSize * BOARD_SIZE) / 2;
    
    return QRect(xOffset + col * squareSize, yOffset + row * squareSize, squareSize, squareSize);
}

QString ChessboardWidget::squareToAlgebraic(int row, int col) const
{
    return QString(QChar('a' + col)) + QString::number(8 - row);
}

void ChessboardWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}
