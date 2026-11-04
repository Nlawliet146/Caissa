#include "uci.hpp"
#include "board.hpp"
#include "search.hpp"
#include "movegen.hpp"
#include "tt.hpp"
#include <iostream>
#include <string>
#include <sstream>

std::string move_to_string(const Move& m) {
    std::string s = "";
    s += (char)('a' + (m.from_sq % 8));
    s += (char)('1' + (m.from_sq / 8));
    s += (char)('a' + (m.to_sq % 8));
    s += (char)('1' + (m.to_sq / 8));
    if (m.promoted_piece == W_QUEEN || m.promoted_piece == B_QUEEN) s += "q";
    if (m.promoted_piece == W_ROOK || m.promoted_piece == B_ROOK) s += "r";
    if (m.promoted_piece == W_BISHOP || m.promoted_piece == B_BISHOP) s += "b";
    if (m.promoted_piece == W_KNIGHT || m.promoted_piece == B_KNIGHT) s += "n";
    return s;
}

Move parse_move(Board& board, const std::string& move_str) {
    std::vector<Move> moves;
    generate_pseudo_legal_moves(board, moves);
    for (const auto& m : moves) {
        if (move_to_string(m) == move_str && board.make_move(m)) {
            board.unmake_move(m);
            return m;
        }
    }
    return {NO_SQ, NO_SQ, EMPTY, 0, 0};
}

void uci_loop() {
    Board board;
    board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    init_zobrist();
    std::string line, token;

    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        is >> token;

        if (token == "uci") {
            std::cout << "id name Caissa\n";
            std::cout << "id author Naman & Pratham\n";
            std::cout << "uciok\n";
        } else if (token == "isready") {
            std::cout << "readyok\n";
        } else if (token == "position") {
            is >> token;
            if (token == "startpos") {
                board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
                is >> token;
            } else if (token == "fen") {
                std::string fen;
                while (is >> token && token != "moves") fen += token + " ";
                board.parse_fen(fen);
            }
            if (token == "moves") {
                std::string move_str;
                while (is >> move_str) {
                    Move m = parse_move(board, move_str);
                    board.make_move(m);
                }
            }
        } else if (token == "go") {
            int depth = 5; // Default search depth
            // Here you can parse "wtime", "btime" etc. if needed later
            Move best = search_root(board, depth);
            std::cout << "bestmove " << move_to_string(best) << "\n";
        } else if (token == "quit") {
            break;
        }
    }
}