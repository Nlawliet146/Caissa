# Caissa
chess engine, that will be using machine learning (probably)

## Phase 3 measurements

Build `build/caissa` against the same Python minor version as the environment
containing `requirements.txt`. Caissa embeds Python, so pass that environment's
site-packages path to the binary as well as using its interpreter for the
scripts. From the repository root, with the project environment active:

```sh
export PYTHONPATH="$(python -c 'import sysconfig; print(sysconfig.get_path("purelib"))')${PYTHONPATH:+:$PYTHONPATH}"
python benchmark.py --json build/phase3-benchmark.json
python python_ml/evaluate_checkpoint.py
python match.py --output build/phase3-baseline.pgn
```

- **Benchmark.** Identical FENs in both modes, with alternating run order,
  a reset transposition table before each search, and model startup outside
  the timed region. The fixed-depth comparison measures work to reach the
  same depth. The fixed-time comparison measures depth reached under the same
  budget. UCI nodes and engine time are from the last completed iteration.
  Wall time measures from sending `go` through receiving `bestmove` and also
  includes any unfinished iteration. The engine reserves 30 ms from each
  `movetime` for overhead.
- **Validation.** Requires the four `.npy` arrays in `data/processed` from
  the checkpoint's training corpus. The script reuses the trainer's
  `game_id % 20 == 0` selection, capped at 50,000 positions. This partition
  was used for checkpoint selection. The checkpoint stores weights only, so
  a matching dataset must be supplied separately. Top-k measures whether
  the human move's from/to label is among the k highest-scoring output
  labels before applying the inference-time legal mask.
- **Match.** Requires Cute Chess CLI 1.5.1. The default uses 50 distinct
  EPD openings, each played with both color assignments, one game at a time.
  Each move has a 60 ms limit with a 30 ms engine reserve. Games still
  ongoing at the 100-full-move cap are adjudicated as draws. The fixture
  comes from the first 50 distinct positions after eight plies in the local
  November 2025 Lichess Elite archive, skipping invalid games and games
  shorter than 16 plies. An alternate suite can be supplied with `--openings`.
  The runner keeps the PGN and its sibling UCI log, checks the color-swapped
  pairs and ML diagnostics, and reports wins, draws, losses, and termination
  reasons. Choose a fresh output path for each run.

The ML score is `(wins + draws / 2) / games`. The two games in a pair share
an opening, so uncertainty about the score should be estimated using opening
pairs as the sampling units.
