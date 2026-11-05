#include "search.hpp"
#include "movegen.hpp"
#include "evaluate.hpp"
#include "tt.hpp"
#include "py_bridge.hpp"
#include <algorithm>

int quiescence(Board& board, int alpha, int beta) {
    int stand_pat = evaluate(board);
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);

    for (const auto& move : move_list) {
        if (move.flags != 1) continue; // Tactical Captures only
        if (!board.make_move(move)) continue;
        int score = -quiescence(board, -beta, -alpha);
        board.unmake_move(move);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

void score_moves(const Board& board, std::vector<Move>& move_list, const Move& tt_move, const std::vector<float>& ml_priors) {
    for (auto& move : move_list) {
        if (move.from_sq == tt_move.from_sq && move.to_sq == tt_move.to_sq) {
            move.score = 100000; // #1 Priority: TT Hit
        } else if (move.flags == 1) { // Capture
            move.score = 50000 + (board.square_board[move.to_sq] * 10) - board.square_board[move.from_sq]; // MVV-LVA
        } else {
            // #2 Priority: Python ML ResNet Priors (with safe bounds check)
            size_t idx = move.from_sq * 64 + move.to_sq;
            if (!ml_priors.empty() && idx < ml_priors.size()) {
                move.score = (int)(ml_priors[idx] * 1000.0f);
            } else {
                move.score = 0; // Safe fallback so it never segfaults if model isn't loaded/trained
            }
        }
    }
    // Descending sort
    std::sort(move_list.begin(), move_list.end(), [](const Move& a, const Move& b) {
        return a.score > b.score;
    });
}
int alpha_beta(Board& board, int depth, int alpha, int beta, const std::vector<float>& ml_priors) {
    int original_alpha = alpha;
    uint64_t hash = generate_hash(board);
    Move tt_move = {NO_SQ, NO_SQ, EMPTY, 0, 0};
    int cached_score;

    if (tt_probe(hash, depth, alpha, beta, cached_score, tt_move)) {
        return cached_score;
    }

    if (depth == 0) return quiescence(board, alpha, beta);

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);
    score_moves(board, move_list, tt_move, ml_priors);

    int legal_moves = 0;
    Move best_move = {NO_SQ, NO_SQ, EMPTY, 0, 0};

    for (const auto& move : move_list) {
        if (!board.make_move(move)) continue;
        legal_moves++;

        // Decrease depth and recursively call. Pass empty priors inside tree to avoid Python overhead.
        static std::vector<float> empty_priors;
        int score = -alpha_beta(board, depth - 1, -beta, -alpha, empty_priors);
        board.unmake_move(move);

        if (score >= beta) {
            tt_store(hash, depth, beta, HASH_BETA, move);
            return beta; 
        }
        if (score > alpha) {
            alpha = score;
            best_move = move;
        }
    }

    if (legal_moves == 0) {
        int king_p = (board.side_to_move == WHITE) ? W_KING : B_KING;
        int king_sq = __builtin_ctzll(board.piece_bitboards[king_p]);
        if (is_square_attacked(board, king_sq, ~board.side_to_move & 1))
            return -40000 + depth; // Checkmate
        return 0; // Stalemate
    }

    int flag = (alpha <= original_alpha) ? HASH_ALPHA : HASH_EXACT;
    tt_store(hash, depth, alpha, flag, best_move);
    return alpha;
}

Move search_root(Board& board, int depth) {
    // Ask Python ONCE, for the root position only.
    std::vector<float> priors = PythonBridge::get_instance().get_priors(board.to_fen());

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);
    if (move_list.empty()) return {NO_SQ, NO_SQ, EMPTY, 0, 0};

    Move empty_tt = {NO_SQ, NO_SQ, EMPTY, 0, 0};
    score_moves(board, move_list, empty_tt, priors);   // captures + ML decide the first order

    // The priors describe the ROOT position only, so deeper nodes must not use them.
    static const std::vector<float> no_priors;

    Move best_move = move_list[0];

    for (int d = 1; d <= depth; d++) {
        int alpha = -50000;
        const int beta = 50000;
        bool found = false;
        Move iter_best = best_move;

        for (const auto& move : move_list) {
            if (!board.make_move(move)) continue;
            int score = -alpha_beta(board, d - 1, -beta, -alpha, no_priors);
            board.unmake_move(move);

            if (!found || score > alpha) {   // a better move raises alpha, so later moves get pruned harder
                found = true;
                alpha = score;
                iter_best = move;
            }
        }
        if (!found) break;
        best_move = iter_best;

        // Next, deeper iteration: try the best move found so far FIRST.
        for (auto it = move_list.begin(); it != move_list.end(); ++it) {
            if (it->from_sq == best_move.from_sq && it->to_sq == best_move.to_sq &&
                it->promoted_piece == best_move.promoted_piece) {
                std::rotate(move_list.begin(), it, it + 1);
                break;
            }
        }
    }
    return best_move;
}