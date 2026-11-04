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

void init_zobrist();
uint64_t generate_hash(const Board& board);
void tt_store(uint64_t key, int depth, int score, int flag, Move best_move);
bool tt_probe(uint64_t key, int depth, int alpha, int beta, int& return_score, Move& return_move);

#endif