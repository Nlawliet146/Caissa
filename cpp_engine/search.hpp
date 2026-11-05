#ifndef SEARCH_HPP
#define SEARCH_HPP

#include <atomic>
#include <chrono>
#include "board.hpp"

// Global flags for UCI communication across translation units
extern std::atomic<bool> stop_search;
extern bool use_ml; // ML Toggle flag

struct SearchLimits {
    int depth = -1;
    int movetime = -1; // Time for this specific move in ms
    int wtime = -1, btime = -1; // Time remaining on clock
    int winc = 0, binc = 0;     // Increment
};

// Time management helpers
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

inline TimePoint get_time() {
    return std::chrono::steady_clock::now();
}

inline int elapsed_ms(TimePoint start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(get_time() - start).count();
}

// Function declarations exposed to uci.cpp
void iterative_deepening(Board& board, SearchLimits limits);
int alpha_beta(Board& board, int depth, int alpha, int beta, int ply, TimePoint start_time, int allocated_time, uint64_t& nodes);

#endif // SEARCH_HPP