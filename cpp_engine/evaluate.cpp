#include "evaluate.hpp"

static const int PieceValues[12] = {
    100, 320, 330, 500, 900, 20000, // White P, N, B, R, Q, K
    100, 320, 330, 500, 900, 20000  // Black P, N, B, R, Q, K
};

static const int PawnPST[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

static const int KnightPST[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};

int evaluate(const Board& board) {
    int score = 0;
    for (int sq = 0; sq < 64; sq++) {
        int p = board.square_board[sq];
        if (p == EMPTY) continue;

        int val = PieceValues[p];
        int pst = 0;
        if (p == W_PAWN) pst = PawnPST[sq];
        else if (p == B_PAWN) pst = PawnPST[63 - sq];
        else if (p == W_KNIGHT) pst = KnightPST[sq];
        else if (p == B_KNIGHT) pst = KnightPST[63 - sq];

        if (p <= W_KING) score += (val + pst);
        else score -= (val + pst);
    }
    return (board.side_to_move == WHITE) ? score : -score;
}