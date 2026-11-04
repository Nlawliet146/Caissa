#include "uci.hpp"
#include "movegen.hpp"
#include "tt.hpp"
#include "py_bridge.hpp"

int main() {
    // 1. Initialize Tables and Boot Python Interpreter
    init_attack_tables();
    init_zobrist();
    PythonBridge::get_instance(); // Warms up Pybind11
    
    // 2. Handover control to the GUI Protocol
    uci_loop();
    return 0;
}