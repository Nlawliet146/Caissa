#include "py_bridge.hpp"
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <iostream>
#include <cstdlib>
#include <filesystem>

namespace py = pybind11;

static py::scoped_interpreter* guard = nullptr;
static py::gil_scoped_release* gil_release = nullptr;
static py::object inference_func;

namespace fs = std::filesystem;

static bool is_project_root(const fs::path& dir) {
    std::error_code error;
    return fs::is_regular_file(dir / "python_ml" / "inference_pipeline.py", error);
}

static fs::path find_project_root(fs::path dir) {
    while (!dir.empty()) {
        if (is_project_root(dir)) return dir;
        if (dir == dir.root_path()) break;
        dir = dir.parent_path();
    }
    return {};
}

static fs::path get_project_root() {
    if (const char* env = std::getenv("CAISSA_ROOT")) {
        if (is_project_root(env)) return env;
    }

    // Linux can also find a checkout after it has been moved.
    std::error_code error;
#ifdef __linux__
    const auto executable = fs::read_symlink("/proc/self/exe", error);
    if (!error) {
        const auto root = find_project_root(executable.parent_path());
        if (!root.empty()) return root;
    }
#endif
    if (is_project_root(CAISSA_SOURCE_DIR)) return CAISSA_SOURCE_DIR;

    const auto root = find_project_root(fs::current_path(error));
    return root.empty() ? fs::path(".") : root;
}

PythonBridge::PythonBridge() {
    try {
        if (!guard) {
            PyConfig config;
            PyConfig_InitPythonConfig(&config);
            // Leave the UCI streams alone even when PYTHONUNBUFFERED is set.
            config.configure_c_stdio = 0;
            guard = new py::scoped_interpreter(&config);
        }
        py::module_ sys = py::module_::import("sys");

        const auto ml_path = (get_project_root() / "python_ml").u8string();
        sys.attr("path").attr("insert")(0, py::str(reinterpret_cast<const char*>(ml_path.data()), ml_path.size()));

        py::module_ pipeline = py::module_::import("inference_pipeline");
        inference_func = pipeline.attr("get_search_priors");
        initialized = true;
    } catch (const std::exception& e) {
        std::cerr << "[PyBridge Warning] Could not load Python ML Pipeline: " << e.what() << "\n";
        std::cerr << "Falling back to pure C++ heuristics.\n";
    }

    // The thread that creates the interpreter owns the GIL. The search runs on
    // ANOTHER thread, so we must give the GIL up here and let get_priors()
    // take it with gil_scoped_acquire. Without this, Python would be entered
    // by a thread that does not hold the GIL, which is undefined behaviour.
    if (guard) gil_release = new py::gil_scoped_release();
}

PythonBridge& PythonBridge::get_instance() {
    static PythonBridge instance;
    return instance;
}

std::vector<float> PythonBridge::get_priors(const std::string& fen) {
    // Fallback zero vector if not initialized
    if (!initialized) return std::vector<float>();

    py::gil_scoped_acquire gil; // declared first => released last (after `result` is gone)
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
    // Must run on the thread that constructed us (the UCI thread == main).
    delete gil_release;            // takes the GIL back on this thread
    gil_release = nullptr;
    inference_func = py::object(); // drop our Python reference while the interpreter is still alive
    delete guard;                  // Py_Finalize()
    guard = nullptr;
}
