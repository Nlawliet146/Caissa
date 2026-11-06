#include "uci.hpp"
#include "movegen.hpp"
#include "tt.hpp"

int main() {
    // 1. Pure C++ tables. The Python/ML bridge is NOT started here any more: it
    //    is created lazily (on "isready"/"go") and only if the ML switch is on,
    //    so a "UseML=false" engine never imports torch at all.
    init_attack_tables();
    init_zobrist();

    // 2. Handover control to the GUI Protocol
    uci_loop();
    return 0;
}
