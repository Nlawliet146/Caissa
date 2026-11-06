#include "uci.hpp"
#include "search.hpp"
#include "movegen.hpp"
#include "tt.hpp"
#include "board.hpp"
#include "py_bridge.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

const char* const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

std::mutex  out_mutex;     // serialises everything written to stdout
bool ml_root_lmr = false;
bool ml_ply_one = false;
std::thread search_thread; // the ONE search thread (joinable => a search may be running)

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tok;
    for (std::string t; iss >> t;) tok.push_back(t);
    return tok;
}

// Tell the search to stop and WAIT until it has printed its bestmove and exited.
void stop_and_join() {
    stop_search = true;
    if (search_thread.joinable()) search_thread.join();
}

// ---- position --------------------------------------------------------------

// Match a UCI move string ("e2e4", "e7e8q") against the generated moves of this
// position and play it. Matching by string means castling (e1g1), en passant
// and promotions all resolve to the exact Move struct our movegen produced.
bool apply_uci_move(Board& board, const std::string& text) {
    const std::string s = lower(text);
    std::vector<Move> moves;
    generate_pseudo_legal_moves(board, moves);
    for (const Move& m : moves) {
        if (move_to_uci(m) == s) return board.make_move(m); // false => leaves own king in check
    }
    return false;
}

void parse_position(const std::string& command, Board& board) {
    const std::vector<std::string> tok = tokenize(command); // tok[0] == "position"
    size_t i = 1;
    std::string fen;

    if (i < tok.size() && tok[i] == "startpos") {
        fen = START_FEN;
        i++;
    } else if (i < tok.size() && tok[i] == "fen") {
        for (++i; i < tok.size() && tok[i] != "moves"; ++i) fen += tok[i] + " ";
    } else {
        return;
    }

    Board next;                // build aside, then commit: a bad line never half-updates `board`
    next.parse_fen(fen);

    if (i < tok.size() && tok[i] == "moves") {
        for (++i; i < tok.size(); ++i) {
            if (!apply_uci_move(next, tok[i])) {
                std::cerr << "[uci] illegal/unknown move in position command: " << tok[i] << "\n";
                break;
            }
        }
    }
    board = next;
}

// ---- go --------------------------------------------------------------------

void run_perft(Board board, int depth) {
    std::vector<Move> moves;
    generate_pseudo_legal_moves(board, moves);
    uint64_t total = 0;
    for (const Move& m : moves) {
        if (!board.make_move(m)) continue;
        const uint64_t n = (depth > 1) ? perft(board, depth - 1) : 1;
        board.unmake_move(m);
        total += n;
        uci_print(move_to_uci(m) + ": " + std::to_string(n));
    }
    uci_print("nodes searched: " + std::to_string(total));
}

void parse_go(const std::string& command, Board& board) {
    SearchLimits limits;
    limits.start       = get_time();            // start the clock NOW, before anything slow happens
    limits.use_ml      = use_ml.load();         // snapshot (see SearchLimits)
    limits.ml_root_lmr = limits.use_ml && ml_root_lmr;
    limits.ml_ply_one = limits.use_ml && ml_ply_one;
    limits.overhead_ms = move_overhead_ms.load();

    const std::vector<std::string> tok = tokenize(command);
    int perft_depth = 0;
    for (size_t i = 1; i < tok.size(); ++i) {
        const std::string& t = tok[i];
        auto next_int = [&](int& dst) {
            if (i + 1 < tok.size()) dst = std::atoi(tok[++i].c_str());
        };
        if      (t == "wtime")     next_int(limits.wtime);
        else if (t == "btime")     next_int(limits.btime);
        else if (t == "winc")      next_int(limits.winc);
        else if (t == "binc")      next_int(limits.binc);
        else if (t == "movestogo") next_int(limits.movestogo);
        else if (t == "depth")     next_int(limits.depth);
        else if (t == "movetime")  next_int(limits.movetime);
        else if (t == "infinite")  limits.infinite = true;
        else if (t == "perft")     next_int(perft_depth);
        // ponder, nodes, mate, searchmoves: accepted and ignored
    }

    stop_and_join();                    // a previous search must be finished first
    stop_search = false;

    if (perft_depth > 0) {              // debugging aid: "go perft 5" (movegen correctness)
        run_perft(board, perft_depth);
        return;
    }

    // Create the Python bridge HERE, on the UCI thread (see py_bridge.cpp: the
    // thread that creates the interpreter must also be the one to destroy it).
    if (limits.use_ml) PythonBridge::get_instance();

    // std::thread COPIES its arguments: the search gets its own Board.
    search_thread = std::thread(iterative_deepening, board, limits);
}

// ---- setoption -------------------------------------------------------------

// "setoption name <words...> [value <words...>]" - names/values may contain spaces.
void parse_setoption(const std::string& command) {
    const std::vector<std::string> tok = tokenize(command);
    std::string name, value;
    bool in_name = false, in_value = false;
    for (size_t i = 1; i < tok.size(); ++i) {
        if (tok[i] == "name")       { in_name = true;  in_value = false; continue; }
        if (tok[i] == "value")      { in_value = true; in_name = false;  continue; }
        if (in_name)  name  += (name.empty()  ? "" : " ") + tok[i];
        if (in_value) value += (value.empty() ? "" : " ") + tok[i];
    }
    name  = lower(name);
    value = lower(value);

    const bool enabled = !(value == "false" || value == "0" || value == "off");
    if (name == "useml") {
        use_ml = enabled;
    } else if (name == "mlrootlmr") {
        ml_root_lmr = enabled;
    } else if (name == "mlplyone") {
        ml_ply_one = enabled;
    } else if (name == "moveoverhead") {
        move_overhead_ms = std::clamp(std::atoi(value.c_str()), 0, 5000);
    }
    // unknown options are ignored, as the UCI spec requires
}

} // namespace

void uci_print(const std::string& line) {
    std::lock_guard<std::mutex> lock(out_mutex);
    std::cout << line << '\n' << std::flush;
}

void uci_loop() {
    // Default for the ML switch: environment variable CAISSA_ML=0|false|off
    // (handy for scripts that cannot send setoption), overridable by the GUI.
    if (const char* env = std::getenv("CAISSA_ML")) {
        const std::string v = lower(env);
        use_ml = !(v == "0" || v == "false" || v == "off");
    }

    Board board;
    board.parse_fen(START_FEN);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "uci") {
            uci_print("id name Caissa");
            uci_print("id author nlawliet");
            uci_print(std::string("option name UseML type check default ") + (use_ml ? "true" : "false"));
            uci_print("option name MLRootLMR type check default false");
            uci_print("option name MLPlyOne type check default false");
            uci_print("option name MoveOverhead type spin default 30 min 0 max 5000");
            uci_print("uciok");
        }
        else if (token == "isready") {
            // "isready" is the GUI's synchronisation point: it waits for readyok
            // however long we take. Slow start-up work (importing torch) belongs here.
            if (use_ml) PythonBridge::get_instance();
            uci_print("readyok");
        }
        else if (token == "ucinewgame") {
            stop_and_join();
            clear_tt();
            board.parse_fen(START_FEN);
        }
        else if (token == "setoption") {
            parse_setoption(line);
        }
        else if (token == "position") {
            parse_position(line, board);
        }
        else if (token == "go") {
            parse_go(line, board);
        }
        else if (token == "stop") {
            stop_search = true;   // the search thread notices, prints bestmove, exits
        }
        else if (token == "quit") {
            break;
        }
    }

    stop_and_join();              // quit OR stdin closed: never exit with a running thread
}
