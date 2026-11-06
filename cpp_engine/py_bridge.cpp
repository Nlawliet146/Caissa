#include "py_bridge.hpp"
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <limits.h>

namespace py = pybind11;

static py::scoped_interpreter* guard = nullptr;
static py::gil_scoped_release* gil_release = nullptr;
static py::object inference_func;

static bool is_project_root(const std::string& dir) {
    return access((dir + "/python_ml/inference_pipeline.py").c_str(), R_OK) == 0;
}

// Find the directory that contains python_ml/. We must NOT depend on the
// working directory: cutechess (and most GUIs) start engines from wherever
// they like, and then "import inference_pipeline" fails and ML silently turns
// off. So: 1) $CAISSA_ROOT, 2) walk up from the binary, 3) walk up from cwd.
static std::string get_project_root() {
    if (const char* env = std::getenv("CAISSA_ROOT")) {
        if (is_project_root(env)) return env;
    }

    auto walk_up = [](std::string dir) -> std::string {
        for (int i = 0; i < 6 && !dir.empty(); ++i) {
            if (is_project_root(dir)) return dir;
            const size_t slash = dir.rfind('/');
            if (slash == std::string::npos || slash == 0) break;
            dir.erase(slash);
        }
        return "";
    };

    char exe[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        std::string dir(exe);
        dir.erase(dir.rfind('/')); // strip the file name: .../build/caissa -> .../build
        const std::string r = walk_up(dir);
        if (!r.empty()) return r;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        const std::string r = walk_up(cwd);
        if (!r.empty()) return r;
    }
    return ".";
}

PythonBridge::PythonBridge() {
    // If PYTHONUNBUFFERED is set (Docker, CI, many IDE setups do this), Py_Initialize()
    // calls setvbuf(stdin, _IONBF) - and glibc then DISCARDS whatever UCI lines
    // std::cin had already buffered (e.g. a "position" + "go" the GUI sent
    // back-to-back). Python must not touch our stdio, so remove the variable first.
    unsetenv("PYTHONUNBUFFERED");

    try {
        if (!guard) guard = new py::scoped_interpreter{};
        py::module_ sys = py::module_::import("sys");

        const std::string ml_path = get_project_root() + "/python_ml";
        sys.attr("path").attr("append")(ml_path);

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
