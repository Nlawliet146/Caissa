#ifndef BOARD_HPP
#define BOARD_HPP

#include <cstdint>
#include <string>
#include <vector>

enum Square {
    A1=0, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8, NO_SQ
};

enum Piece {
    W_PAWN = 0, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING, EMPTY
};

enum Color { WHITE = 0, BLACK = 1, BOTH = 2 };

enum CastlingFlags { WK = 1, WQ = 2, BK = 4, BQ = 8 };

struct Move {
    uint8_t from_sq;
    uint8_t to_sq;
    uint8_t promoted_piece;
    uint8_t flags; // 0: Normal, 1: Capture, 2: EnPassant, 3: Castling
    int score;     // For move ordering
};

struct UndoState {
    int castling_rights;
    int en_passant_sq;
    int halfmove_clock;
    int captured_piece;
};

class Board {
public:
    uint64_t piece_bitboards[12];
    uint64_t color_bitboards[3];
    int square_board[64]; // MAILBOX ARRAY

    int side_to_move;
    int castling_rights;
    int en_passant_sq;
    int halfmove_clock;
    int fullmove_number;

    std::vector<UndoState> history;

    Board();
    void clear();
    void parse_fen(const std::string& fen);
    std::string to_fen() const;
    void print_board() const;

    bool make_move(const Move& m);
    void unmake_move(const Move& m);
};

#endif