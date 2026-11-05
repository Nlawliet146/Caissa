#include "search.hpp"
#include "movegen.hpp"
#include "evaluate.hpp"
#include "tt.hpp"
#include "py_bridge.hpp"
#include <algorithm>
#include <iostream>

// Global flags for UCI communication
extern std::atomic<bool> stop_search;
extern bool use_ml;

// Helper to convert your Move struct to UCI string format (e.g., e2e4)
// Implement this based on how your engine stores squares (e.g., using an array of square names)
std::string move_to_uci(const Move& move) {
    // Example: return square_to_string(move.from_sq) + square_to_string(move.to_sq);
    return "e2e4"; // REPLACE WITH YOUR ACTUAL MOVE-TO-STRING LOGIC
}

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
                move.score = 0; 
            }
        }
    }
    std::sort(move_list.begin(), move_list.end(), [](const Move& a, const Move& b) {
        return a.score > b.score;
    });
}

// Notice the updated signature passing the time variables down the tree
int alpha_beta(Board& board, int depth, int alpha, int beta, const std::vector<float>& ml_priors, TimePoint start_time, int allocated_time, uint64_t& nodes) {
    // 1. Time Management Check: Do not poll time every node to save NPS
    nodes++;
    if ((nodes & 2047) == 0) {
        if (allocated_time > 0 && elapsed_ms(start_time) >= allocated_time) {
            stop_search = true;
        }
    }
    if (stop_search) return 0; // Safe unwind

    int original_alpha = alpha;
    uint64_t hash = generate_hash(board);
    Move tt_move = {NO_SQ, NO_SQ, EMPTY, 0, 0};
    int cached_score;

    if (tt_probe(hash, depth, alpha, beta, cached_score, tt_move)) {
        return cached_score;
    }

    if (depth <= 0) return quiescence(board, alpha, beta);

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);
    score_moves(board, move_list, tt_move, ml_priors);

    int legal_moves = 0;
    Move best_move = {NO_SQ, NO_SQ, EMPTY, 0, 0};

    for (const auto& move : move_list) {
        if (!board.make_move(move)) continue;
        legal_moves++;

        // Decrease depth and recursively call. Pass empty priors inside tree.
        static std::vector<float> empty_priors;
        int score = -alpha_beta(board, depth - 1, -beta, -alpha, empty_priors, start_time, allocated_time, nodes);
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
        // Ensure your WHITE / BLACK constants match your implementation
        int king_p = (board.side_to_move == 0) ? W_KING : B_KING;
        int king_sq = __builtin_ctzll(board.piece_bitboards[king_p]);
        if (is_square_attacked(board, king_sq, ~board.side_to_move & 1))
            return -40000 + depth; // Checkmate
        return 0; // Stalemate
    }

    int flag = (alpha <= original_alpha) ? HASH_ALPHA : HASH_EXACT;
    tt_store(hash, depth, alpha, flag, best_move);
    return alpha;
}

// This function replaces your previous 'search_root' function
void iterative_deepening(Board& board, SearchLimits limits) {
    TimePoint start_time = get_time();
    int allocated_time = -1;

    // Allocate time dynamically based on UCI limits
    if (limits.movetime > 0) {
        allocated_time = limits.movetime - 50;
    } 
    else if (board.side_to_move == 0 && limits.wtime > 0) {
        allocated_time = (limits.wtime / 20) + (limits.winc / 2);
    } 
    else if (board.side_to_move == 1 && limits.btime > 0) {
        allocated_time = (limits.btime / 20) + (limits.binc / 2);
    }

    int max_depth = (limits.depth > 0) ? limits.depth : 100;
    uint64_t nodes = 0;

    // Ask Python ONCE for the root position, strictly if use_ml is enabled by the GUI
    std::vector<float> priors;
    if (use_ml) {
        priors = PythonBridge::get_instance().get_priors(board.to_fen());
    }

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);
    
    if (move_list.empty()) {
        std::cout << "bestmove 0000" << std::endl;
        return;
    }

    Move empty_tt = {NO_SQ, NO_SQ, EMPTY, 0, 0};
    score_moves(board, move_list, empty_tt, priors);

    static const std::vector<float> no_priors;
    Move best_move_overall = move_list[0];
    int best_score_overall = 0;

    for (int d = 1; d <= max_depth; d++) {
        if (stop_search) break;

        int alpha = -50000;
        int beta = 50000;
        bool found = false;
        Move iter_best = best_move_overall;

        for (const auto& move : move_list) {
            if (!board.make_move(move)) continue;
            
            // Execute depth-first search
            int score = -alpha_beta(board, d - 1, -beta, -alpha, no_priors, start_time, allocated_time, nodes);
            board.unmake_move(move);

            // Abort instantly if time ran out mid-branch to avoid polluting the score
            if (stop_search) break;

            if (!found || score > alpha) {
                found = true;
                alpha = score;
                iter_best = move;
            }
        }

        if (stop_search) break;
        if (!found) break;

        best_score_overall = alpha;
        best_move_overall = iter_best;

        int time_used = elapsed_ms(start_time);
        
        // Output standardized UCI info
        std::cout << "info depth " << d
                  << " score cp " << best_score_overall
                  << " nodes " << nodes
                  << " time " << time_used
                  << std::endl;

        // Next iteration: Try the best move found so far FIRST
        for (auto it = move_list.begin(); it != move_list.end(); ++it) {
            if (it->from_sq == best_move_overall.from_sq && 
                it->to_sq == best_move_overall.to_sq &&
                it->promoted_piece == best_move_overall.promoted_piece) {
                std::rotate(move_list.begin(), it, it + 1);
                break;
            }
        }

        if (allocated_time > 0 && time_used >= allocated_time) {
            break;
        }
    }

    std::cout << "bestmove " << move_to_uci(best_move_overall) << std::endl;
}