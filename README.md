# Caissa

Caissa is a chess engine built as a B.Tech capstone project. It combines
classical chess search with a neural network that predicts moves people are
likely to play. The project asks whether those predictions help a search
find better moves within the same time budget.

The C++ engine represents the board with bitboards, generates legal moves,
and searches possible replies using iterative deepening and alpha-beta
pruning. A Python model supplies move priorities through pybind11. Caissa
tries the prioritized moves earlier, then chooses its move using the search
score. A UCI-compatible chess GUI can send positions and display the
engine's chosen move. Personalized opponent modeling is future work.

## Build and run

On Linux, install a C++17 compiler, CMake, and Python with matching
development headers and virtual environment support. From the repository root:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
cmake -S . -B build -DPython3_EXECUTABLE="$VIRTUAL_ENV/bin/python" \
  -Dpybind11_DIR="$(python -m pybind11 --cmakedir)"
cmake --build build -j
export PYTHONPATH="$(python -c 'import sysconfig; print(sysconfig.get_path("purelib"))')${PYTHONPATH:+:$PYTHONPATH}"
./build/caissa
```

The shipped checkpoint is loaded automatically when ML is enabled. In a chess
GUI, launch the engine with the same environment. Set `UseML` to `false` in
the GUI or start the engine with `CAISSA_ML=0` for classical ordering.

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
