#include "tt.hpp"
#include <random>
#include <cstring>

TTEntry TranspositionTable[TT_SIZE];
uint64_t ZobristPieces[12][64];
uint64_t ZobristSide;
uint64_t ZobristCastle[16];
uint64_t ZobristEP[8];

static inline int score_to_tt(int s, int ply) {
    if (s >= MATE_BOUND)  return s + ply;
    if (s <= -MATE_BOUND) return s - ply;
    return s;
}
static inline int score_from_tt(int s, int ply) {
    if (s >= MATE_BOUND)  return s - ply;
    if (s <= -MATE_BOUND) return s + ply;
    return s;
}

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
    for (int i = 0; i < 16; i++) ZobristCastle[i] = rng();
    for (int i = 0; i < 8; i++)  ZobristEP[i] = rng();
    
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
    hash ^= ZobristCastle[board.castling_rights & 15];
    if (board.en_passant_sq != NO_SQ) {
        hash ^= ZobristEP[board.en_passant_sq & 7];
    }
    return hash;
}

void tt_store(uint64_t key, int depth, int score, int flag, Move best_move, int ply) {
    int index = key & (TT_SIZE - 1);
    // Always replace scheme for simplicity
    TranspositionTable[index].key = key;
    TranspositionTable[index].depth = depth;
    TranspositionTable[index].score = score_to_tt(score, ply);
    TranspositionTable[index].flag = flag;
    TranspositionTable[index].best_move = best_move;
}

bool tt_probe(uint64_t key, int depth, int alpha, int beta, int& return_score, Move& return_move, int ply) {
    int index = key & (TT_SIZE - 1);
    TTEntry* entry = &TranspositionTable[index];

    if (entry->key == key) {
        return_move = entry->best_move;
        if (entry->depth >= depth) {
            const int s = score_from_tt(entry->score, ply);
            if (entry->flag == HASH_EXACT) {
                return_score = s;
                return true;
            }
            if (entry->flag == HASH_ALPHA && s <= alpha) {
                return_score = alpha;
                return true;
            }
            if (entry->flag == HASH_BETA && s >= beta) {
                return_score = beta;
                return true;
            }
        }
    }
    return false;
}