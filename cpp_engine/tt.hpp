#ifndef TT_HPP
#define TT_HPP

#include "board.hpp"
#include <cstdint>

enum TTFlag { HASH_EXACT = 0, HASH_ALPHA = 1, HASH_BETA = 2 };

struct TTEntry {
    uint64_t key;
    int depth;
    int score;
    int flag;
    Move best_move;
};

// 1 Megabyte TT Size (Power of 2 for fast bitwise modulo)
const int TT_SIZE = 1048576; 

constexpr int MATE_SCORE = 40000;
constexpr int MAX_PLY    = 128;
constexpr int MATE_BOUND = MATE_SCORE - MAX_PLY;

extern uint64_t ZobristPieces[12][64];
extern uint64_t ZobristSide;
extern uint64_t ZobristCastle[16];
extern uint64_t ZobristEP[8]; // indexed by en-passant file

void init_zobrist();
uint64_t generate_hash(const Board& board);
// Mate scores are stored relative to the node (ply-adjusted) so they stay valid across transpositions.
void tt_store(uint64_t key, int depth, int score, int flag, Move best_move, int ply);
bool tt_probe(uint64_t key, int depth, int alpha, int beta, int& return_score, Move& return_move, int ply);
void clear_tt();
#endif