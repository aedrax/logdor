#include "chessgame.h"
#include <QRegularExpression>

ChessGame::ChessGame()
{
    reset();
}

void ChessGame::reset()
{
    m_board.clear();
    m_whiteToMove = true;
    m_whiteCanCastleKingside = true;
    m_whiteCanCastleQueenside = true;
    m_blackCanCastleKingside = true;
    m_blackCanCastleQueenside = true;
    m_enPassantSquare.clear();
    m_halfmoveClock = 0;
    m_fullmoveNumber = 1;
    
    initializeBoard();
}

void ChessGame::initializeBoard()
{
    // Place white pieces
    m_board["a1"] = "wR"; m_board["b1"] = "wN"; m_board["c1"] = "wB"; m_board["d1"] = "wQ";
    m_board["e1"] = "wK"; m_board["f1"] = "wB"; m_board["g1"] = "wN"; m_board["h1"] = "wR";
    for (int col = 0; col < 8; ++col) {
        m_board[QString(QChar('a' + col)) + "2"] = "wP";
    }
    
    // Place black pieces
    m_board["a8"] = "bR"; m_board["b8"] = "bN"; m_board["c8"] = "bB"; m_board["d8"] = "bQ";
    m_board["e8"] = "bK"; m_board["f8"] = "bB"; m_board["g8"] = "bN"; m_board["h8"] = "bR";
    for (int col = 0; col < 8; ++col) {
        m_board[QString(QChar('a' + col)) + "7"] = "bP";
    }
}

bool ChessGame::makeMove(const QString& move)
{
    static QRegularExpression moveRegex(
        // Piece (optional)
        "([KQRBN])?"
        // Source file or rank (optional)
        "([a-h]|[1-8])?"
        // Capture mark (optional)
        "(x)?"
        // Destination square (required)
        "([a-h][1-8])"
        // Promotion (optional)
        "(=[QRBN])?"
        // Check/mate (optional)
        "[+#]?"
    );
    QRegularExpressionMatch match = moveRegex.match(move);
    
    if (!match.hasMatch()) {
        // Check for castling
        if (move == "O-O") {
            if (m_whiteToMove) {
                if (!m_whiteCanCastleKingside) return false;
                movePiece(algebraicToSquare("e1"), algebraicToSquare("g1"));
                movePiece(algebraicToSquare("h1"), algebraicToSquare("f1"));
            } else {
                if (!m_blackCanCastleKingside) return false;
                movePiece(algebraicToSquare("e8"), algebraicToSquare("g8"));
                movePiece(algebraicToSquare("h8"), algebraicToSquare("f8"));
            }
            m_whiteToMove = !m_whiteToMove;
            if (!m_whiteToMove) m_fullmoveNumber++;
            return true;
        }
        if (move == "O-O-O") {
            if (m_whiteToMove) {
                if (!m_whiteCanCastleQueenside) return false;
                movePiece(algebraicToSquare("e1"), algebraicToSquare("c1"));
                movePiece(algebraicToSquare("a1"), algebraicToSquare("d1"));
            } else {
                if (!m_blackCanCastleQueenside) return false;
                movePiece(algebraicToSquare("e8"), algebraicToSquare("c8"));
                movePiece(algebraicToSquare("a8"), algebraicToSquare("d8"));
            }
            m_whiteToMove = !m_whiteToMove;
            if (!m_whiteToMove) m_fullmoveNumber++;
            return true;
        }
        return false;
    }
    
    // Parse the move components
    QString pieceType = match.captured(1);
    QString sourceSquare = match.captured(2);
    bool isCapture = !match.captured(3).isEmpty();
    QString destSquare = match.captured(4);
    QString promotion = match.captured(5);

    // Default to pawn if no piece specified
    if (pieceType.isEmpty()) pieceType = "P";
    QString piece = (m_whiteToMove ? "w" : "b") + pieceType;
    
    // Get destination square
    Square dest = algebraicToSquare(destSquare);
    if (!dest.isValid()) return false;
    
    // Find source piece considering source square hint
    Square src = findPiece(piece, dest, sourceSquare);
    if (!src.isValid()) return false;
    
    // Verify capture
    if (isCapture && squareToPiece(dest).isEmpty()) return false;
    if (!isCapture && !squareToPiece(dest).isEmpty()) return false;
    
    if (!isValidMove(piece, src, dest)) return false;
    
    movePiece(src, dest);
    updateCastlingRights(piece, src);
    
    m_whiteToMove = !m_whiteToMove;
    if (!m_whiteToMove) m_fullmoveNumber++;
    
    return true;
}

QString ChessGame::getFen() const
{
    QString fen;
    
    // Piece placement
    for (int row = 0; row < 8; ++row) {
        int emptyCount = 0;
        for (int col = 0; col < 8; ++col) {
            QString piece = squareToPiece(Square(row, col));
            if (piece.isEmpty()) {
                emptyCount++;
            } else {
                if (emptyCount > 0) {
                    fen += QString::number(emptyCount);
                    emptyCount = 0;
                }
                QChar pieceChar = piece[1];
                fen += piece[0] == 'w' ? pieceChar : pieceChar.toLower();
            }
        }
        if (emptyCount > 0) {
            fen += QString::number(emptyCount);
        }
        if (row < 7) fen += '/';
    }
    
    // Active color
    fen += m_whiteToMove ? " w " : " b ";
    
    // Castling availability
    QString castling;
    if (m_whiteCanCastleKingside) castling += 'K';
    if (m_whiteCanCastleQueenside) castling += 'Q';
    if (m_blackCanCastleKingside) castling += 'k';
    if (m_blackCanCastleQueenside) castling += 'q';
    fen += castling.isEmpty() ? "- " : castling + " ";
    
    // En passant target square
    fen += m_enPassantSquare.isEmpty() ? "- " : m_enPassantSquare + " ";
    
    // Halfmove clock and fullmove number
    fen += QString::number(m_halfmoveClock) + " " + QString::number(m_fullmoveNumber);
    
    return fen;
}

ChessGame::Square ChessGame::findPiece(const QString& piece, const Square& dest, const QString& srcFile) const
{
    for (auto it = m_board.begin(); it != m_board.end(); ++it) {
        if (it.value() == piece) {
            Square src = algebraicToSquare(it.key());
            if (!srcFile.isEmpty() && src.col != srcFile[0].toLatin1() - 'a') continue;
            if (isValidMove(piece, src, dest)) return src;
        }
    }
    return Square();
}

bool ChessGame::isValidMove(const QString& piece, const Square& src, const Square& dest) const
{
    // Basic validation - source and destination must be different squares
    if (!src.isValid() || !dest.isValid() || (src.row == dest.row && src.col == dest.col))
        return false;
    
    // Check if piece belongs to current player
    if (!isPieceOfCurrentPlayer(piece))
        return false;
    
    // Check if destination is occupied by own piece
    QString destPiece = squareToPiece(dest);
    if (!destPiece.isEmpty() && destPiece[0] == piece[0])  // Same color
        return false;
    
    // Get piece type
    QChar pieceType = piece[1];
    
    // Validate move based on piece type
    int rowDiff = dest.row - src.row;
    int colDiff = dest.col - src.col;
    int rowAbs = abs(rowDiff);
    int colAbs = abs(colDiff);
    
    switch (pieceType.toLatin1()) {
        case 'P': {  // Pawn
            int forward = (piece[0] == 'w') ? -1 : 1;  // White moves up (-row), Black moves down (+row)
            bool isCapture = !destPiece.isEmpty();
            
            // Normal move
            if (colDiff == 0 && !isCapture) {
                if (rowDiff == forward) return true;
                // Initial two-square move
                if ((src.row == 6 && piece[0] == 'w') || (src.row == 1 && piece[0] == 'b')) {
                    if (rowDiff == 2 * forward) {
                        // Check if path is clear
                        Square middle(src.row + forward, src.col);
                        return squareToPiece(middle).isEmpty();
                    }
                }
            }
            // Capture
            else if (colAbs == 1 && rowDiff == forward && isCapture) {
                return true;
            }
            return false;
        }
        case 'N':  // Knight
            return (rowAbs == 2 && colAbs == 1) || (rowAbs == 1 && colAbs == 2);
            
        case 'B':  // Bishop
            return rowAbs == colAbs && isPathClear(src, dest);
            
        case 'R':  // Rook
            return (rowDiff == 0 || colDiff == 0) && isPathClear(src, dest);
            
        case 'Q':  // Queen
            return (rowDiff == 0 || colDiff == 0 || rowAbs == colAbs) && isPathClear(src, dest);
            
        case 'K':  // King
            return rowAbs <= 1 && colAbs <= 1;
    }
    
    return false;
}

bool ChessGame::isPathClear(const Square& src, const Square& dest) const
{
    int rowStep = (dest.row > src.row) ? 1 : (dest.row < src.row) ? -1 : 0;
    int colStep = (dest.col > src.col) ? 1 : (dest.col < src.col) ? -1 : 0;
    
    Square current(src.row + rowStep, src.col + colStep);
    while (current.row != dest.row || current.col != dest.col) {
        if (!squareToPiece(current).isEmpty())
            return false;
        current.row += rowStep;
        current.col += colStep;
    }
    
    return true;
}

void ChessGame::movePiece(const Square& src, const Square& dest)
{
    QString srcSquare = src.toAlgebraic();
    QString destSquare = dest.toAlgebraic();
    
    if (m_board.contains(srcSquare)) {
        m_board[destSquare] = m_board[srcSquare];
        m_board.remove(srcSquare);
    }
}

void ChessGame::updateCastlingRights(const QString& piece, const Square& src)
{
    // King move
    if (piece == "wK") {
        m_whiteCanCastleKingside = false;
        m_whiteCanCastleQueenside = false;
    }
    else if (piece == "bK") {
        m_blackCanCastleKingside = false;
        m_blackCanCastleQueenside = false;
    }
    // Rook move
    else if (piece == "wR") {
        if (src.toAlgebraic() == "a1") m_whiteCanCastleQueenside = false;
        if (src.toAlgebraic() == "h1") m_whiteCanCastleKingside = false;
    }
    else if (piece == "bR") {
        if (src.toAlgebraic() == "a8") m_blackCanCastleQueenside = false;
        if (src.toAlgebraic() == "h8") m_blackCanCastleKingside = false;
    }
}

QString ChessGame::squareToPiece(const Square& square) const
{
    return m_board.value(square.toAlgebraic());
}

ChessGame::Square ChessGame::algebraicToSquare(const QString& algebraic) const
{
    if (algebraic.length() != 2) return Square();
    
    int col = algebraic[0].toLatin1() - 'a';
    int row = '8' - algebraic[1].toLatin1();
    
    if (col < 0 || col > 7 || row < 0 || row > 7) return Square();
    
    return Square(row, col);
}

bool ChessGame::isPieceOfCurrentPlayer(const QString& piece) const
{
    return piece.startsWith(m_whiteToMove ? 'w' : 'b');
}
