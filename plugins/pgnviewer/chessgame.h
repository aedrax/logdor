#ifndef CHESSGAME_H
#define CHESSGAME_H

#include <QString>
#include <QMap>
#include <QChar>

class ChessGame {
public:
    ChessGame();
    
    // Reset to starting position
    void reset();
    
    // Make a move in algebraic notation (e.g., "e4", "Nf3", "O-O")
    bool makeMove(const QString& move);
    
    // Get current position in FEN notation
    QString getFen() const;
    
private:
    struct Square {
        int row;    // 0-7 (1-8)
        int col;    // 0-7 (a-h)
        
        Square(int r = -1, int c = -1) : row(r), col(c) {}
        bool isValid() const { return row >= 0 && row < 8 && col >= 0 && col < 8; }
        QString toAlgebraic() const { 
            return isValid() ? QString(QChar('a' + col)) + QString::number(8 - row) : QString();
        }
    };
    
    // Board representation (key: algebraic notation e.g. "e4", value: piece e.g. "wP" for white pawn)
    QMap<QString, QString> m_board;
    
    bool m_whiteToMove;
    bool m_whiteCanCastleKingside;
    bool m_whiteCanCastleQueenside;
    bool m_blackCanCastleKingside;
    bool m_blackCanCastleQueenside;
    QString m_enPassantSquare;
    int m_halfmoveClock;
    int m_fullmoveNumber;
    
    // Helper methods
    void initializeBoard();
    Square findPiece(const QString& piece, const Square& dest, const QString& srcFile = QString()) const;
    bool isValidMove(const QString& piece, const Square& src, const Square& dest) const;
    bool isPathClear(const Square& src, const Square& dest) const;
    void movePiece(const Square& src, const Square& dest);
    void updateCastlingRights(const QString& piece, const Square& src);
    QString squareToPiece(const Square& square) const;
    Square algebraicToSquare(const QString& algebraic) const;
    bool isPieceOfCurrentPlayer(const QString& piece) const;
};

#endif // CHESSGAME_H
