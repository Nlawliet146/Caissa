#include "search.hpp"
#include "movegen.hpp"
#include "evaluate.hpp"
#include "tt.hpp"
#include "py_bridge.hpp"
#include "uci.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Shared flags (declared extern in search.hpp)
// ---------------------------------------------------------------------------
std::atomic<bool> stop_search{false};
std::atomic<bool> use_ml{true};
std::atomic<int>  move_overhead_ms{30};

namespace {

constexpr int MAX_DEPTH = 64;
// MATE_SCORE / MATE_BOUND / MAX_PLY live in tt.hpp

// Everything one search needs to carry down the tree besides the board.
struct SearchState {
    TimePoint start{};
    int64_t   hard_ms = 0;   // 0 = no hard limit
    uint64_t  nodes   = 0;   // alpha_beta + quiescence nodes
    size_t    root_hist = 0; // board.history.size() at the root (repetition scope)
};

// True if side to move has at least one legal move.
bool has_legal_move(Board& board) {
    std::vector<Move> ml;
    generate_pseudo_legal_moves(board, ml);
    for (const Move& m : ml) {
        if (!board.make_move(m)) continue;
        board.unmake_move(m);
        return true;
    }
    return false;
}

bool in_check(const Board& board) {
    const int king_p = (board.side_to_move == WHITE) ? W_KING : B_KING;
    const uint64_t kb = board.piece_bitboards[king_p];
    if (!kb) return false;
    return is_square_attacked(board, __builtin_ctzll(kb), board.side_to_move ^ 1);
}

// Counts one node and decides whether the search must unwind.
// The clock is read only every 2048 nodes (a clock read costs ~20-30 ns,
// a node ~0.5-1 us); the stop flag is read every node (a plain load).
inline bool must_stop(SearchState& ss) {
    ss.nodes++;
    if ((ss.nodes & 2047) == 0 && ss.hard_ms > 0 && elapsed_ms(ss.start) >= ss.hard_ms)
        stop_search.store(true);
    return stop_search.load();
}

} // namespace

// ---------------------------------------------------------------------------
// Time management
// ---------------------------------------------------------------------------
TimeBudget compute_time_budget(const SearchLimits& lim, int side) {
    TimeBudget tb;
    const int64_t overhead = lim.overhead_ms;

    if (lim.infinite) return tb;                       // only "stop" ends it

    if (lim.movetime > 0) {                            // exact time per move
        tb.hard_ms = std::max<int64_t>(1, lim.movetime - overhead);
        return tb;                                     // soft stays 0: use all of it
    }

    const int64_t T   = (side == WHITE) ? lim.wtime : lim.btime;
    const int64_t inc = (side == WHITE) ? lim.winc  : lim.binc;
    if (T < 0) return tb;                              // depth-only / no clock given

    const int64_t mtg   = (lim.movestogo > 0) ? std::min(lim.movestogo, 50) : 30;
    const int64_t avail = std::max<int64_t>(1, T - overhead);   // never plan to spend the last `overhead` ms
    tb.soft_ms = std::max<int64_t>(1, std::min(avail, T / mtg + inc * 3 / 4));
    tb.hard_ms = std::min(avail, tb.soft_ms * 3);
    return tb;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------
namespace {

int quiescence(Board& board, int alpha, int beta, SearchState& ss) {
    if (must_stop(ss)) return 0;

    int stand_pat = evaluate(board);
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);

    for (const auto& move : move_list) {
        if (move.flags != 1) continue; // Tactical Captures only
        if (!board.make_move(move)) continue;
        int score = -quiescence(board, -beta, -alpha, ss);
        board.unmake_move(move);
        if (stop_search.load()) return 0;

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
    // stable_sort: equal scores keep move-generation order, so "ML off" is
    // deterministic and the ONLY difference between ML on/off is the priors.
    std::stable_sort(move_list.begin(), move_list.end(), [](const Move& a, const Move& b) {
        return a.score > b.score;
    });
}

int alpha_beta(Board& board, int depth, int alpha, int beta, int ply,
               const std::vector<float>& ml_priors, SearchState& ss) {
    if (must_stop(ss)) return 0; // Safe unwind

    // Draw detection (path-dependent: must precede the TT probe).
    if (ply > 0) {
        if (board.is_repetition(ss.root_hist)) return 0;
        if (board.is_fifty_move_draw()) {
            // Checkmate on the 100th half-move still beats the draw.
            if (!in_check(board) || has_legal_move(board)) return 0;
            return -MATE_SCORE + ply;
        }
    }

    int original_alpha = alpha;
    const uint64_t hash = board.hash;
    Move tt_move = {NO_SQ, NO_SQ, EMPTY, 0, 0};
    int cached_score;

    if (tt_probe(hash, depth, alpha, beta, cached_score, tt_move, ply)) {
        return cached_score;
    }

    if (depth <= 0) return quiescence(board, alpha, beta, ss);

    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);
    score_moves(board, move_list, tt_move, ml_priors);

    int legal_moves = 0;
    Move best_move = {NO_SQ, NO_SQ, EMPTY, 0, 0};

    for (const auto& move : move_list) {
        if (!board.make_move(move)) continue;
        legal_moves++;

        // Decrease depth and recursively call. Pass empty priors inside tree.
        static const std::vector<float> empty_priors;
        int score = -alpha_beta(board, depth - 1, -beta, -alpha, ply + 1, empty_priors, ss);
        board.unmake_move(move);

        // A child that was told to stop returns a dummy 0. That 0 is NOT a
        // score: if we used it we could raise alpha, cause a fake cutoff and,
        // worst of all, write the lie into the transposition table, where it
        // would poison every later search. So: unwind WITHOUT touching the TT.
        if (stop_search.load()) return 0;

        if (score >= beta) {
            tt_store(hash, depth, beta, HASH_BETA, move, ply);
            return beta;
        }
        if (score > alpha) {
            alpha = score;
            best_move = move;
        }
    }

    if (legal_moves == 0) {
        // Ensure your WHITE / BLACK constants match your implementation
        if (in_check(board))
            return -MATE_SCORE + ply; // Checkmate: nearer mates score lower for the mated side
        return 0; // Stalemate
    }

    int flag = (alpha <= original_alpha) ? HASH_ALPHA : HASH_EXACT;
    tt_store(hash, depth, alpha, flag, best_move, ply);
    return alpha;
}

} // namespace

// ---------------------------------------------------------------------------
// Iterative deepening driver (runs on the search thread)
// ---------------------------------------------------------------------------
void iterative_deepening(Board board, SearchLimits limits) {
    SearchState ss;
    ss.start = limits.start;
    const TimeBudget budget = compute_time_budget(limits, board.side_to_move);
    ss.hard_ms = budget.hard_ms;
    ss.root_hist = board.history.size();

    // 1. Legal root moves. generate_pseudo_legal_moves() may contain moves that
    //    leave our king in check; make_move() tells us (and undoes itself).
    //    Using the pseudo-legal list here would print an ILLEGAL "bestmove" in
    //    checkmate/stalemate instead of "bestmove 0000".
    std::vector<Move> root_moves;
    {
        std::vector<Move> pseudo;
        generate_pseudo_legal_moves(board, pseudo);
        for (const Move& m : pseudo) {
            if (!board.make_move(m)) continue;
            board.unmake_move(m);
            root_moves.push_back(m);
        }
    }
    if (root_moves.empty()) {
        uci_print("bestmove 0000");
        return;
    }

    // 2. ML priors: ONE Python call at the root, only if enabled for this "go".
    //    The time spent here is charged to the move clock (ss.start is the
    //    moment "go" arrived), so ML-vs-no-ML at equal time is an honest test.
    std::vector<float> priors;
    if (limits.use_ml) {
        const TimePoint t0 = get_time();
        priors = PythonBridge::get_instance().get_priors(board.to_fen());
        const auto nonzero = std::count_if(priors.begin(), priors.end(), [](float p) { return p > 0.0f; });
        if (nonzero == 0) {
            // Never let "ML on" silently degrade into "ML off": say so.
            uci_print("info string ML requested but NO priors available (bridge/weights failed) - searching without ML");
        } else {
            uci_print("info string ML priors ok: " + std::to_string(nonzero) + " moves, " +
                      std::to_string(elapsed_ms(t0)) + " ms");
        }
    } else {
        uci_print("info string ML off");
    }

    Move empty_tt = {NO_SQ, NO_SQ, EMPTY, 0, 0};
    score_moves(board, root_moves, empty_tt, priors);

    static const std::vector<float> no_priors;
    Move best_move_overall = root_moves[0]; // a legal fallback if "stop" arrives instantly
    int best_score_overall = 0;
    const int max_depth = (limits.depth > 0) ? std::min(limits.depth, MAX_DEPTH) : MAX_DEPTH;

    for (int d = 1; d <= max_depth; d++) {
        if (stop_search.load()) break;

        int alpha = -50000;
        const int beta = 50000;
        bool found = false;          // at least one root move FINISHED this iteration
        Move iter_best = best_move_overall;

        for (const auto& move : root_moves) {
            if (!board.make_move(move)) continue;
            int score = -alpha_beta(board, d - 1, -beta, -alpha, 1, no_priors, ss);
            board.unmake_move(move);

            // Aborted mid-branch: this score is a dummy, discard it.
            if (stop_search.load()) break;

            if (!found || score > alpha) {
                found = true;
                alpha = score;
                iter_best = move;
            }
        }

        // Moves that completed before an abort carry exact scores under the
        // full window, and the previous best move is searched first, so the
        // best of them is never worse than the previous iteration's choice.
        // Keeping it is sound; throwing away a half-done iteration is not
        // needed.
        if (found) {
            best_move_overall = iter_best;
            best_score_overall = alpha;
        }
        if (stop_search.load()) break;   // incomplete iteration: no info line

        const int64_t t = elapsed_ms(ss.start);
        const uint64_t nps = (t > 0) ? ss.nodes * 1000 / static_cast<uint64_t>(t) : 0;
        std::string score_str;
        if (std::abs(best_score_overall) >= MATE_BOUND) {
            const int plies = MATE_SCORE - std::abs(best_score_overall);
            const int moves = (plies + 1) / 2;
            score_str = "mate " + std::to_string(best_score_overall > 0 ? moves : -moves);
        } else {
            score_str = "cp " + std::to_string(best_score_overall);
        }
        uci_print("info depth " + std::to_string(d) +
                  " score " + score_str +
                  " nodes " + std::to_string(ss.nodes) +
                  " nps " + std::to_string(nps) +
                  " time " + std::to_string(t) +
                  " pv " + move_to_uci(best_move_overall));

        // Next iteration: try the best move found so far FIRST
        for (auto it = root_moves.begin(); it != root_moves.end(); ++it) {
            if (it->from_sq == best_move_overall.from_sq &&
                it->to_sq == best_move_overall.to_sq &&
                it->promoted_piece == best_move_overall.promoted_piece) {
                std::rotate(root_moves.begin(), it, it + 1);
                break;
            }
        }

        // Soft limit. If iteration times grow by a factor b per depth, then after
        // finishing depth d (elapsed E) depth d+1 ends at about b*E. We start it
        // only while E < soft/2, so it still finishes before the hard limit
        // (3*soft) whenever b <= 3*soft / (soft/2) = 6. Measured b on this engine
        // is 3.6-6.8, so a few iterations are cut off by the hard limit; the
        // partial-iteration salvage above makes that cheap.
        if (budget.soft_ms > 0 && t * 2 >= budget.soft_ms) break;
    }

    // UCI: after "go infinite" the engine must NOT answer until "stop" arrives,
    // even if the search ran out of depth/moves.
    if (limits.infinite) {
        while (!stop_search.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    uci_print("bestmove " + move_to_uci(best_move_overall));
}
