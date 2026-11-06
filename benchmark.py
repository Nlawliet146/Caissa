"""Paired UCI searches with a cold transposition table and warmed ML model."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
from queue import Empty, Queue
import re
from statistics import mean
import subprocess
from threading import Thread
from time import monotonic

ROOT = Path(__file__).resolve().parent
POSITIONS = (
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
)
INFO = re.compile(r"^info depth (\d+).*? nodes (\d+).*? time (\d+)")


def engine_env():
    return dict(os.environ, CAISSA_ROOT=str(ROOT), CAISSA_ML="0")


@contextmanager
def engine(binary, use_ml, timeout, ml_option=None):
    with subprocess.Popen(
        [str(Path(binary).resolve())], env=engine_env(), text=True,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    ) as proc:
        lines = Queue()

        def read():
            for line in proc.stdout:
                lines.put(line.strip())
            lines.put(None)

        reader = Thread(target=read, daemon=True)
        reader.start()

        def send(command):
            print(command, file=proc.stdin, flush=True)

        def until(prefix):
            deadline = monotonic() + timeout
            response = []
            while True:
                try:
                    line = lines.get(timeout=max(0, deadline - monotonic()))
                except Empty as error:
                    raise TimeoutError(f"Engine did not send {prefix} within {timeout:g}s") from error
                if line is None:
                    raise RuntimeError(f"Engine exited waiting for {prefix}")
                response.append(line)
                if line.startswith(prefix):
                    return response

        try:
            send("uci")
            until("uciok")
            send(f"setoption name UseML value {str(use_ml).lower()}")
            if ml_option:
                send(f"setoption name {ml_option} value true")
            send("isready")
            until("readyok")
            yield send, until
        finally:
            if proc.poll() is None:
                try:
                    send("quit")
                    proc.wait(timeout=3)
                except (BrokenPipeError, subprocess.TimeoutExpired):
                    proc.kill()
                    proc.wait()
            reader.join()


def search(session, fen, limit, use_ml):
    send, until = session
    send("ucinewgame")
    send("position fen " + fen)
    send("isready")
    until("readyok")
    start = monotonic()
    send("go " + limit)
    lines = until("bestmove ")
    wall_ms = 1000 * (monotonic() - start)
    expected = "info string ML priors ok:" if use_ml else "info string ML off"
    if not any(line.startswith(expected) for line in lines):
        raise RuntimeError(f"Expected {expected!r}; got {lines}")
    iterations = [match for line in lines if (match := INFO.match(line))]
    depth, nodes, engine_ms = map(int, iterations[-1].groups()) if iterations else (0, None, None)
    telemetry = next((line.split()[3:] for line in lines if line.startswith("info string phase4 ")), [])
    stats = {key: int(value) for key, value in zip(telemetry[::2], telemetry[1::2])}
    return dict(depth=depth, nodes=nodes, engine_ms=engine_ms, wall_ms=wall_ms,
                bestmove=lines[-1].split()[1], **stats)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", default=ROOT / "build/caissa")
    parser.add_argument("--depth", type=int, default=5)
    parser.add_argument("--movetime", type=int, default=200, help="milliseconds")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--experiment", choices=("MLRootLMR", "MLPlyOne"),
                        help="compare one experiment against root ordering with ML enabled in both")
    parser.add_argument("--json", type=Path, help="write all individual measurements")
    args = parser.parse_args()
    if min(args.depth, args.movetime, args.repeats, args.timeout) <= 0:
        parser.error("limits, repeats and timeout must be positive")
    rows = []
    control_ml = bool(args.experiment)
    with (
        engine(args.engine, control_ml, args.timeout) as control,
        engine(args.engine, True, args.timeout, args.experiment) as treatment,
    ):
        for limit in (f"depth {args.depth}", f"movetime {args.movetime}"):
            for repeat in range(args.repeats):
                for index, fen in enumerate(POSITIONS):
                    for variant in ([False, True] if (repeat + index) % 2 == 0 else [True, False]):
                        result = search(treatment if variant else control, fen, limit, variant or control_ml)
                        if args.experiment and variant and "reductions" not in result:
                            raise RuntimeError(f"No Phase 4 telemetry for {args.experiment}")
                        rows.append(dict(limit=limit, fen=fen, repeat=repeat, ml=variant or control_ml,
                                         experiment=args.experiment if variant else None, **result))
            for use_ml in (False, True):
                group = [row for row in rows if row["limit"] == limit
                         and (bool(row["experiment"]) if args.experiment else row["ml"]) == use_ml]
                label = (args.experiment or "ML on") if use_ml else ("Root ordering" if control_ml else "ML off")
                print(f"{limit}, {label}, {len(group)} searches")
                for field in ("depth", "nodes", "engine_ms", "wall_ms", "reductions", "researches",
                              "ply_one_calls", "ply_one_usable", "ply_one_us"):
                    values = [row[field] for row in group if row.get(field) is not None]
                    if not values and field not in ("depth", "nodes", "engine_ms", "wall_ms"):
                        continue
                    value = f"{mean(values):.2f}" if values else "unavailable"
                    print(f"  mean {field}: {value} ({len(values)}/{len(group)} recorded)")
    if args.json:
        args.json.write_text(json.dumps(rows, indent=2) + "\n")


if __name__ == "__main__":
    main()
