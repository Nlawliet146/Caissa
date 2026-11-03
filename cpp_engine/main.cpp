#include "board.hpp"
#include "movegen.hpp"
#include "search.hpp"
#include <iostream>

int main() {
    init_attack_tables();

    Board board;
    board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    std::cout << "==========================================" << std::endl;
    std::cout << "   CAISSA CHESS ENGINE (C++ BACKEND)      " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Starting FEN: " << board.to_fen() << std::endl;

    std::cout << "\n[RUNNING PERFT VALIDATION TEST...]" << std::endl;
    uint64_t nodes = perft(board, 5);
    std::cout << "Perft Depth 5 Result: " << nodes << " nodes" << std::endl;
    
    if (nodes == 4865609) {
        std::cout << ">>> PERFT PASSED SUCCESSFULLY (100% LEGAL MOVES) <<<" << std::endl;
    } else {
        std::cout << ">>> PERFT FAILED! Expected: 4865609 <<<" << std::endl;
        return 1;
    }

    std::cout << "\n[RUNNING TEST SEARCH (DEPTH 4)...]" << std::endl;
    Move best = search_root(board, 4);
    char f1 = 'a' + (best.from_sq % 8), r1 = '1' + (best.from_sq / 8);
    char f2 = 'a' + (best.to_sq % 8), r2 = '1' + (best.to_sq / 8);
    std::cout << "Best Initial Move: " << f1 << r1 << f2 << r2 << std::endl;

    return 0;
}