#include "uci.hpp"
#include "search.hpp"
#include "tt.hpp"
#include "board.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

std::atomic<bool> stop_search(false);
bool use_ml = true; // Default to ML on

void parse_go(const std::string& command, Board& board) {
    SearchLimits limits;
    std::istringstream iss(command);
    std::string token;
    iss >> token; // skip "go"

    while (iss >> token) {
        if (token == "wtime") iss >> limits.wtime;
        else if (token == "btime") iss >> limits.btime;
        else if (token == "winc") iss >> limits.winc;
        else if (token == "binc") iss >> limits.binc;
        else if (token == "depth") iss >> limits.depth;
        else if (token == "movetime") iss >> limits.movetime;
    }

    // Reset the stop flag before starting
    stop_search = false;

    // Start search in a separate thread so the UCI loop can continue listening for "stop"
    std::thread search_thread(iterative_deepening, std::ref(board), limits);
    search_thread.detach(); 
}

void parse_position(const std::string& command, Board& board) {
    std::istringstream iss(command);
    std::string token;
    iss >> token; // skip "position"
    iss >> token;

    if (token == "startpos") {
        // CALL YOUR STARTING BOARD SETUP HERE
        // Example: board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        
        iss >> token; // consume "moves" if it exists in the string
    } 
    else if (token == "fen") {
        std::string fen;
        while (iss >> token && token != "moves") {
            fen += token + " ";
        }
        // CALL YOUR FEN PARSER HERE
        // Example: board.parse_fen(fen);
    }

    // Parse and execute the list of moves that follow the starting position
    while (iss >> token) {
        // CALL YOUR MOVE PARSER/EXECUTOR HERE
        // Example: board.make_move(token);
    }
}

void uci_loop() {
    std::string line;
    Board board;

    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "uci") {
            std::cout << "id name Caissa\n";
            std::cout << "id author nlawliet\n";
            std::cout << "option name UseML type check default true\n";
            std::cout << "uciok\n" << std::flush;
        } 
        else if (token == "isready") {
            std::cout << "readyok\n" << std::flush;
        } 
        else if (token == "ucinewgame") {
            clear_tt(); // Calling the standalone function we fixed in tt.cpp
        } 
        else if (token == "setoption") {
            if (line.find("UseML") != std::string::npos) {
                if (line.find("false") != std::string::npos) {
                    use_ml = false;
                } else if (line.find("true") != std::string::npos) {
                    use_ml = true;
                }
            }
        } 
        else if (token == "position") {
            parse_position(line, board);
        } 
        else if (token == "go") {
            parse_go(line, board);
        } 
        else if (token == "stop") {
            stop_search = true;
        } 
        else if (token == "quit") {
            stop_search = true;
            break;
        }
    }
}