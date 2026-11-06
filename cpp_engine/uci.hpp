#ifndef UCI_HPP
#define UCI_HPP

#include <string>

void uci_loop();

// Thread-safe "write one line to the GUI". Both the UCI thread (readyok, ...)
// and the search thread (info, bestmove) print, so every line goes through one
// mutex and is flushed immediately. Never write to std::cout directly.
void uci_print(const std::string& line);

#endif
