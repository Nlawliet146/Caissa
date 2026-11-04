#include "py_bridge.hpp"
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <iostream>
#include <filesystem>

namespace py = pybind11;
namespace fs = std::filesystem;

// Locates python_ml by starting at the folder that contains the executable
// and walking upward. Independent of the working directory (Linux).
static std::string find_python_dir() {
    std::error_code ec;
    fs::path dir = fs::read_symlink("/proc/self/exe", ec).parent_path();
    while (!ec && !dir.empty()) {
        if (fs::is_directory(dir / "python_ml")) return (dir / "python_ml").string();
        if (dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    return (fs::current_path() / "python_ml").string();   // last-resort fallback
}

// Global scoped interpreter ensures Python boots only once and stays alive
static py::scoped_interpreter* guard = nullptr;
static py::object inference_func;

PythonBridge::PythonBridge() {
    try {
        if (!guard) guard = new py::scoped_interpreter{};
        py::module_ sys = py::module_::import("sys");
        // Point Python to Pratham's ML directory
        sys.attr("path").attr("insert")(0, find_python_dir());
        
        py::module_ pipeline = py::module_::import("inference_pipeline");
        inference_func = pipeline.attr("get_search_priors");
        initialized = true;
    } catch (const std::exception& e) {
        std::cerr << "[PyBridge Warning] Could not load Python ML Pipeline: " << e.what() << "\n";
        std::cerr << "Falling back to pure C++ heuristics.\n";
    }
}

PythonBridge& PythonBridge::get_instance() {
    static PythonBridge instance;
    return instance;
}

std::vector<float> PythonBridge::get_priors(const std::string& fen) {
    // Graceful fallback if ML isn't ready
    if (!initialized) return std::vector<float>(4096, 0.0f);
    
    try {
        py::object result = inference_func(fen);
        return result.cast<std::vector<float>>();
    } catch (...) {
        return std::vector<float>(4096, 0.0f);
    }
}

PythonBridge::~PythonBridge() {
    if (guard) {
        delete guard;
        guard = nullptr;
    }
}