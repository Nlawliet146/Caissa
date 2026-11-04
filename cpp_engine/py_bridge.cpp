#include "py_bridge.hpp"
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <iostream>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>

namespace py = pybind11;

static py::scoped_interpreter* guard = nullptr;
static py::object inference_func;

// Helper to get the absolute path of the directory where the binary is running, then find project root
std::string get_project_root() {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        std::string path(result, count);
        // path is something like /path/to/Caissa/build/caissa
        // We need to go up twice: out of 'build/' to reach the project root
        char* dir1 = dirname(&path[0]);
        // Note: dirname modifies in-place, so let's use a safer approach or standard string manipulation:
    }
    
    // Fallback/Standard C++ string manipulation based on current working directory or executable path
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        std::string current(cwd);
        // If run from build/ folder, project root is parent directory
        if (current.find("build") != std::string::npos) {
            // Find last occurrence of build and truncate
            size_t pos = current.rfind("build");
            if (pos != std::string::npos) {
                return current.substr(0, pos);
            }
        }
        return current + "/";
    }
    return "./";
}

PythonBridge::PythonBridge() {
    try {
        if (!guard) guard = new py::scoped_interpreter{};
        py::module_ sys = py::module_::import("sys");
        
        // Dynamically locate the project root and add python_ml to Python's search path
        std::string root = get_project_root();
        std::string ml_path = root + "python_ml";
        
        sys.attr("path").attr("append")(ml_path);
        
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
    // Fallback zero vector if not initialized
    if (!initialized) return std::vector<float>();
    
    try {
        py::object result = inference_func(fen);
        
        // Ensure Python actually returned a valid list or array, not None
        if (result.is_none()) {
            return std::vector<float>();
        }
        
        return result.cast<std::vector<float>>();
    } catch (const std::exception& e) {
        std::cerr << "[PyBridge Error] Exception during inference: " << e.what() << "\n";
        return std::vector<float>();
    }
}

PythonBridge::~PythonBridge() {
    if (guard) {
        delete guard;
        guard = nullptr;
    }
}