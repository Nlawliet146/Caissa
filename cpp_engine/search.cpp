#include "search.hpp"
#include "movegen.hpp"
#include "evaluate.hpp"
#include <algorithm>

int quiescence(Board& board, int alpha, int beta) {
    int stand_pat = evaluate(board);
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);

    for (const auto& move : move_list) {
        if (move.flags != 1) continue; // Only process captures
        if (!board.make_move(move)) continue;
        int score = -quiescence(board, -beta, -alpha);
        board.unmake_move(move);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

int alpha_beta(Board& board, int depth, int alpha, int beta) {
    if (depth == 0) return quiescence(board, alpha, beta);

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);

    int legal_moves = 0;
    for (const auto& move : move_list) {
        if (!board.make_move(move)) continue;
        legal_moves++;

        int score = -alpha_beta(board, depth - 1, -beta, -alpha);
        board.unmake_move(move);

        if (score >= beta) return beta; // Beta cutoff
        if (score > alpha) alpha = score;
    }

    if (legal_moves == 0) {
        int king_p = (board.side_to_move == WHITE) ? W_KING : B_KING;
        int king_sq = __builtin_ctzll(board.piece_bitboards[king_p]);
        if (is_square_attacked(board, king_sq, ~board.side_to_move & 1))
            return -40000 + depth; // Checkmate
        return 0; // Stalemate
    }

    return alpha;
}

Move search_root(Board& board, int depth) {
    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);

    Move best_move = move_list[0];
    int best_score = -50000;

    for (const auto& move : move_list) {
        if (!board.make_move(move)) continue;
        int score = -alpha_beta(board, depth - 1, -50000, 50000);
        board.unmake_move(move);

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }
    }
    return best_move;
}