#ifndef CHESSBOARDWIDGET_H
#define CHESSBOARDWIDGET_H

#include <QWidget>
#include <QMap>
#include <QPainter>

class ChessboardWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChessboardWidget(QWidget* parent = nullptr);
    
    // Set the current position using FEN notation
    void setPosition(const QString& fen);
    
    // Reset to starting position
    void resetBoard();
    
    // Get minimum size hint for layout
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    static const int BOARD_SIZE = 8;
    static const int MARGIN = 30;  // Space for coordinates
    
    // Board colors
    QColor m_lightSquareColor;
    QColor m_darkSquareColor;
    
    // Current position in FEN
    QString m_currentFen;
    
    // Piece positions (key: square like "e4", value: piece like "wP" for white pawn)
    QMap<QString, QString> m_pieces;
    
    // Cached piece images
    QMap<QString, QPixmap> m_pieceImages;
    
    // Helper methods
    void loadPieceImages();
    void drawBoard(QPainter& painter);
    void drawPieces(QPainter& painter);
    QRect squareRect(int row, int col) const;
    QString squareToAlgebraic(int row, int col) const;
    void parseFen(const QString& fen);
};

#endif // CHESSBOARDWIDGET_H
