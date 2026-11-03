#ifndef MOVEGEN_HPP
#define MOVEGEN_HPP

#include "board.hpp"
#include <vector>

void init_attack_tables();
bool is_square_attacked(const Board& board, int sq, int attacker_color);
void generate_pseudo_legal_moves(const Board& board, std::vector<Move>& move_list);
uint64_t perft(Board& board, int depth);

#endif