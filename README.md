# Caissa

Caissa is a chess engine built as a B.Tech capstone project. It combines
classical chess search with a neural network that predicts moves people are
likely to play. The project asks whether those predictions help a search
find better moves within the same time budget.

## How it works

1. **Read the position.** A chess GUI or terminal sends the board and move
   history over UCI. The C++ engine represents the position with bitboards
   and uses them to generate and check moves.
2. **Choose what to search first.** The optional PyTorch model predicts move
   priorities from the position. The engine calls it through pybind11.
   The model was trained to predict moves from human games.
3. **Search and play.** The C++ engine explores replies at increasing depths.
   Alpha-beta pruning skips branches that cannot improve the current result,
   and a transposition table reuses information from positions already
   searched. The engine returns the best move it found within the limit.

Personalized opponent modeling is future work.

## Build and run

Start in the cloned repository or an extracted GitHub source ZIP. If you
already have a C++20 compiler, CMake 3.20 or newer, and a CPython installation
with development headers and virtual environment support, go straight to the
build commands for your shell. Use a Python installation that matches your
compiler target architecture and has PyTorch wheels for your platform.

### Install missing tools

| Platform | First-time setup |
| --- | --- |
| Linux | On Debian or Ubuntu, run `sudo apt install build-essential cmake python3 python3-dev python3-venv`. On other distributions install the equivalent packages. |
| macOS | Run `xcode-select --install` for Apple Clang. Install [CMake](https://cmake.org/download/) and a native [Python](https://www.python.org/downloads/macos/) if needed. Verify with `clang++ --version`, `cmake --version`, and `python3 --version`. |
| Windows | Install [Visual Studio Build Tools](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line) with **Desktop development with C++** and a Windows SDK. Install [CMake](https://cmake.org/download/) and [64-bit Python](https://www.python.org/downloads/windows/) for an x64 build. Open Developer PowerShell for Visual Studio and check `cl`, `cmake --version`, and `py -3 --version`. |

### Linux and macOS

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$VIRTUAL_ENV/bin/python" \
  -Dpybind11_DIR="$(python -m pybind11 --cmakedir)"
cmake --build build --parallel
export PYTHONPATH="$(python -c 'import sysconfig; print(sysconfig.get_path("purelib"))')${PYTHONPATH:+:$PYTHONPATH}"
./build/caissa
```

### Windows (Developer PowerShell)

```powershell
py -3 -m venv .venv
$python = (Resolve-Path .\.venv\Scripts\python.exe).Path
& $python -m pip install -r requirements.txt
$pybind11 = & $python -m pybind11 --cmakedir
cmake -S . -B build "-DPython3_EXECUTABLE=$python" "-Dpybind11_DIR=$pybind11"
cmake --build build --config Release --parallel
$env:PYTHONPATH = & $python -c "import sysconfig; print(sysconfig.get_path('purelib'))"
.\build\Release\caissa.exe
```

The Windows commands use a Visual Studio generator. With a single-config
generator such as Ninja, configure with `-DCMAKE_BUILD_TYPE=Release` and run
`.\build\caissa.exe`.

### Run with or without the model

The shipped checkpoint loads automatically when ML is enabled. Launch a GUI
from the same shell if you want it to inherit the Python environment. If you
move the checkout and the engine cannot find the model code, set `CAISSA_ROOT`
to the checkout's absolute path.

For a classical-only build, or if [PyTorch](https://pytorch.org/get-started/locally/)
has no wheel for your system, install `pybind11` and `python-chess` in place of
the full requirements. Set `CAISSA_ML=0` before launching the engine
(`export CAISSA_ML=0` in a POSIX shell or `$env:CAISSA_ML = "0"` in PowerShell).
You can also toggle `UseML` in a UCI GUI.

For a terminal demo, enter these commands in the running engine. Wait for
`readyok` before sending the position.

```text
uci
isready
position startpos
go depth 4
```

The engine prints search depth, score, nodes and time, then returns
`bestmove`. To compare classical ordering, send
`setoption name UseML value false`, then `ucinewgame`, and repeat the same
position and search. Send `quit` to exit.

## Tests and measurement

```sh
python tests/uci_smoke.py ./build/caissa
python benchmark.py --json build/benchmark.json
python match.py --output build/match.pgn
```

On Windows, use `& $python` and pass `--engine .\build\Release\caissa.exe`
to the two measurement scripts. The smoke test takes the executable as a
positional argument.

- **`tests/uci_smoke.py`** checks the UCI protocol, legal move generation,
  search limits, and the bridge to Python.
- **`benchmark.py`** runs the same positions with ML on and off. It compares
  nodes and time at equal depth, and depth reached at equal time. It answers
  whether the model changes the amount of search work or the depth reached.
- **`match.py`** uses Cute Chess CLI 1.5.1 to play both versions against each
  other under the same time control. It swaps colors for each opening and
  records wins, draws, and losses in a PGN and log. It measures playing
  strength under the chosen settings. Choose a fresh output path for each run.

Training is optional. The scripts in `python_ml/` prepare PGN data, train
checkpoints, and evaluate them. The shipped weights are enough to run the
engine and the demo.
