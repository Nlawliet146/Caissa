#ifndef SEARCH_HPP
#define SEARCH_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include "board.hpp"

// ---- Flags shared between the UCI thread and the search thread ------------
// Defined in search.cpp. All are atomics: two threads touch them.
extern std::atomic<bool> stop_search;      // set by "stop"/"quit"/time-out, read every node
extern std::atomic<bool> use_ml;           // UCI option "UseML" (snapshotted into SearchLimits at "go")
extern std::atomic<int>  move_overhead_ms; // UCI option "MoveOverhead"

// ---- Clock helpers ---------------------------------------------------------
using TimePoint = std::chrono::steady_clock::time_point;

inline TimePoint get_time() { return std::chrono::steady_clock::now(); }

inline int64_t elapsed_ms(TimePoint start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(get_time() - start).count();
}

// ---- What the GUI asked for ("go ...") ------------------------------------
struct SearchLimits {
    int depth     = -1;   // go depth N
    int movetime  = -1;   // go movetime N (ms)
    int wtime     = -1;   // go wtime N (ms left for White; -1 = not given)
    int btime     = -1;
    int winc      = 0;
    int binc      = 0;
    int movestogo = -1;   // go movestogo N
    bool infinite = false;

    // Snapshots taken on the UCI thread at the moment "go" is read, so one
    // search is internally consistent even if setoption arrives mid-search.
    bool use_ml      = true;
    int  overhead_ms = 30;
    TimePoint start{};    // the clock starts when the "go" line was READ
};

// Soft = "don't start a new iteration past this"; hard = "abort mid-iteration".
// 0 means "no limit of this kind".
struct TimeBudget {
    int64_t soft_ms = 0;
    int64_t hard_ms = 0;
};

TimeBudget compute_time_budget(const SearchLimits& limits, int side_to_move);

// Runs on its own thread. Takes the Board BY VALUE: the search owns a private
// copy, so the UCI thread may keep parsing "position" without racing it.
// Prints "info ..." lines and finally "bestmove ..." through uci_print().
void iterative_deepening(Board board, SearchLimits limits);

#endif // SEARCH_HPP
