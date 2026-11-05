#include "tt.hpp"
#include <random>
#include <cstring>

TTEntry TranspositionTable[TT_SIZE];
uint64_t ZobristPieces[12][64];
uint64_t ZobristSide;

void clear_tt() {
    memset(TranspositionTable, 0, TT_SIZE * sizeof(TTEntry));
}

void init_zobrist() {
    std::mt19937_64 rng(1337); // Seeded PRNG for reproducible hashes
    for (int p = 0; p < 12; p++) {
        for (int sq = 0; sq < 64; sq++) {
            ZobristPieces[p][sq] = rng();
        }
    }
    ZobristSide = rng();
    
    // Clear TT
    for (int i = 0; i < TT_SIZE; i++) {
        TranspositionTable[i].key = 0;
    }
}

uint64_t generate_hash(const Board& board) {
    uint64_t hash = 0;
    for (int sq = 0; sq < 64; sq++) {
        int piece = board.square_board[sq];
        if (piece != EMPTY) {
            hash ^= ZobristPieces[piece][sq];
        }
    }
    if (board.side_to_move == BLACK) {
        hash ^= ZobristSide;
    }
    return hash;
}

void tt_store(uint64_t key, int depth, int score, int flag, Move best_move) {
    int index = key & (TT_SIZE - 1);
    // Always replace scheme for simplicity
    TranspositionTable[index].key = key;
    TranspositionTable[index].depth = depth;
    TranspositionTable[index].score = score;
    TranspositionTable[index].flag = flag;
    TranspositionTable[index].best_move = best_move;
}

bool tt_probe(uint64_t key, int depth, int alpha, int beta, int& return_score, Move& return_move) {
    int index = key & (TT_SIZE - 1);
    TTEntry* entry = &TranspositionTable[index];

    if (entry->key == key) {
        return_move = entry->best_move;
        if (entry->depth >= depth) {
            if (entry->flag == HASH_EXACT) {
                return_score = entry->score;
                return true;
            }
            if (entry->flag == HASH_ALPHA && entry->score <= alpha) {
                return_score = alpha;
                return true;
            }
            if (entry->flag == HASH_BETA && entry->score >= beta) {
                return_score = beta;
                return true;
            }
        }
    }
    return false;
}