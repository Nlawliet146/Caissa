#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "board.hpp"

int alpha_beta(Board& board, int depth, int alpha, int beta);
Move search_root(Board& board, int depth);

#endif