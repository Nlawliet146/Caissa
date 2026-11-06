"""Run a color-swapped Cute Chess match and verify its PGN and ML diagnostics."""
import argparse
from collections import Counter
from pathlib import Path
import re
import subprocess

import chess.pgn

from benchmark import POSITIONS, ROOT, engine, engine_env, search

ML = "Caissa-ML"
BASE = "Caissa-NoML"


def report(pgn, expected_games, baseline=BASE):
    results = Counter()
    openings = Counter()
    with pgn.open(encoding="utf-8") as handle:
        while (game := chess.pgn.read_game(handle)) is not None:
            white, black = game.headers["White"], game.headers["Black"]
            result = game.headers["Result"]
            if game.errors or {white, black} != {ML, baseline}:
                raise RuntimeError("Invalid match PGN")
            openings[game.board().epd(), white] += 1
            if result == "1/2-1/2":
                results["draw"] += 1
            elif result in ("1-0", "0-1"):
                winner = white if result == "1-0" else black
                results["win" if winner == ML else "loss"] += 1
            else:
                raise RuntimeError(f"Incomplete game: {result}")
    wins, draws, losses = (results[key] for key in ("win", "draw", "loss"))
    games = wins + draws + losses
    if games != expected_games:
        raise RuntimeError(f"Expected {expected_games} games, got {games}")
    if len(openings) != games or any(
        count != 1 or openings[position, baseline if white == ML else ML] != 1
        for (position, white), count in openings.items()
    ):
        raise RuntimeError("Expected each opening exactly twice with colors swapped")
    print(f"ML: {wins} wins, {draws} draws, {losses} losses | score {(wins + draws / 2) / games:.1%}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=ROOT / "build/caissa")
    parser.add_argument("--cutechess", default="cutechess-cli")
    parser.add_argument(
        "--openings", type=Path, default=ROOT / "tests/fixtures/phase3-openings.epd",
        help="EPD with distinct starting positions",
    )
    parser.add_argument("--output", type=Path, required=True, help="new PGN path")
    parser.add_argument("--root-lmr", action="store_true", help="compare MLRootLMR against root ordering")
    parser.add_argument("--games", type=int, default=100)
    parser.add_argument("--seconds-per-move", type=float, default=0.06)
    parser.add_argument("--max-moves", type=int, default=100, help="adjudicate ongoing games as draws at this limit")
    args = parser.parse_args()
    if args.games < 2 or args.games % 2 or args.seconds_per_move <= 0.030 or args.max_moves < 1:
        parser.error("use an even game count, more than 30 ms per move and a positive move cap")
    openings = [line.split()[:4] for line in args.openings.read_text().splitlines() if line.strip()]
    if len(openings) != len({tuple(position) for position in openings}) or len(openings) < args.games // 2:
        parser.error("supply at least one distinct EPD position per pair")
    log_path = args.output.with_suffix(".log")
    if args.output.exists() or log_path.exists():
        parser.error("choose a new output path")
    with engine(args.engine, True, 60) as session:
        search(session, POSITIONS[0], "depth 1", True)
    binary = str(args.engine.resolve())
    baseline = "Caissa-RootOrdering" if args.root_lmr else BASE
    baseline_ml = str(args.root_lmr).lower()
    variant_options = ["option.MLRootLMR=true"] if args.root_lmr else []
    command = [
        args.cutechess,
        "-engine", f"name={ML}", f"cmd={binary}", "option.UseML=true", *variant_options,
        "-engine", f"name={baseline}", f"cmd={binary}", f"option.UseML={baseline_ml}",
        "-each", "proto=uci", "restart=off", f"st={args.seconds_per_move}", "option.MoveOverhead=30",
        "-debug", "all", "-games", "2", "-rounds", str(args.games // 2), "-repeat",
        "-openings", f"file={args.openings.resolve()}", "format=epd", "order=sequential",
        "-concurrency", "1", "-maxmoves", str(args.max_moves),
        "-pgnout", str(args.output.resolve()),
    ]
    with log_path.open("x", encoding="utf-8") as log:
        result = subprocess.run(command, env=engine_env(), stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Cute Chess exited with status {result.returncode}; see {log_path}")
    audit_log(log_path, args.games, baseline, args.root_lmr)
    report(args.output, args.games, baseline)


def audit_log(path, expected_games, baseline=BASE, root_lmr=False):
    searches = Counter()
    reasons = Counter()
    traffic = re.compile(rf"<({ML}|{re.escape(baseline)})\(\d+\): (.*)")
    with path.open(encoding="utf-8") as log:
        for line in log:
            if line.startswith("Finished game ") and "{" in line:
                reasons[line.split("{", 1)[1].rstrip().rstrip("}")] += 1
            match = traffic.search(line)
            if not match:
                continue
            player, message = match.groups()
            if message.startswith("bestmove "):
                searches[player, "bestmove"] += 1
            elif message.startswith("info string phase4 "):
                if not root_lmr or player != ML:
                    raise RuntimeError(f"Unexpected Phase 4 telemetry from {player}: {message}")
                searches[player, "phase4"] += 1
                fields = message.split()
                searches[player, "reductions"] += int(fields[4])
                searches[player, "researches"] += int(fields[6])
            elif message.startswith("info string ML "):
                expected = "info string ML priors ok:" if player == ML or root_lmr else "info string ML off"
                if not message.startswith(expected):
                    raise RuntimeError(f"Unexpected ML status from {player}: {message}")
                searches[player, "status"] += 1
    for player in (ML, baseline):
        if not searches[player, "status"] or searches[player, "status"] != searches[player, "bestmove"]:
            raise RuntimeError(f"Missing search diagnostics for {player}: {searches}")
    if sum(reasons.values()) != expected_games:
        raise RuntimeError(f"Missing game termination reasons in {path}")
    if root_lmr:
        if searches[ML, "phase4"] != searches[ML, "status"]:
            raise RuntimeError("Missing root LMR telemetry for one or more searches")
        if not searches[ML, "reductions"]:
            raise RuntimeError("No root reductions recorded in the match")
        print(f"Root reductions: {searches[ML, 'reductions']}, full re-searches: {searches[ML, 'researches']}")
    print(f"Verified searches: {ML} {searches[ML, 'status']}, {baseline} {searches[baseline, 'status']}")
    for reason, count in sorted(reasons.items()):
        print(f"  {count:3d} {reason}")


if __name__ == "__main__":
    main()
