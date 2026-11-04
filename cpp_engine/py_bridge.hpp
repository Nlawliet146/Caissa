#ifndef PY_BRIDGE_HPP
#define PY_BRIDGE_HPP

#include <vector>
#include <string>

class PythonBridge {
public:
    static PythonBridge& get_instance();
    std::vector<float> get_priors(const std::string& fen);
    ~PythonBridge();

private:
    PythonBridge();
    bool initialized = false;
};

#endif