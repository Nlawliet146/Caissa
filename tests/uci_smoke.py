#!/usr/bin/env python3
"""
UCI smoke test for Caissa (Phase 1).  Usage:  python3 tests/uci_smoke.py [./build/caissa]

Drives the real binary over pipes, exactly like a GUI would, and checks the
protocol behaviour we promised: go parameters, stop, ucinewgame, info lines,
the ML switch, and move generation against python-chess (if installed).
Exit code 0 = everything passed.
"""
import os
import queue
import random
import subprocess
import sys
import threading
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/caissa"
failures = []


def check(ok, what, detail=""):
    print(("  PASS  " if ok else "  FAIL  ") + what + (f"   [{detail}]" if detail and not ok else ""))
    if not ok:
        failures.append(what)


class Engine:
    def __init__(self, env=None):
        e = dict(os.environ)
        e.update(env or {})
        self.p = subprocess.Popen([BIN], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1, env=e)
        self.q = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.p.stdout:
            self.q.put((time.monotonic(), line.rstrip("\n")))

    def send(self, s):
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def wait_for(self, prefix, timeout=30):
        """Collect lines until one starts with prefix. Returns (lines, t_of_match)."""
        lines, end = [], time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                t, line = self.q.get(timeout=0.05)
            except queue.Empty:
                continue
            lines.append(line)
            if line.startswith(prefix):
                return lines, t
        return lines, None

    def quit(self):
        try:
            self.send("quit")
            return self.p.wait(timeout=10)
        except Exception:
            self.p.kill()
            return None


def go(e, cmd, timeout=30):
    t0 = time.monotonic()
    e.send(cmd)
    lines, t = e.wait_for("bestmove", timeout)
    return lines, (None if t is None else (t - t0) * 1000.0)


def bestmove(lines):
    for l in lines:
        if l.startswith("bestmove"):
            return l.split()[1]
    return None


def perft_total(e, depth):
    e.send(f"go perft {depth}")
    lines, _ = e.wait_for("nodes searched")
    return int(lines[-1].split(":")[1]) if lines and lines[-1].startswith("nodes searched") else -1


print("== handshake")
e = Engine({"CAISSA_ML": "0"})   # ML off: these tests need no Python at all
e.send("uci")
lines, _ = e.wait_for("uciok")
check("uciok" in lines[-1], "uci -> uciok")
check(any("option name UseML type check default false" in l for l in lines), "UseML advertised (env CAISSA_ML=0 => default false)")
check(any("option name MoveOverhead" in l for l in lines), "MoveOverhead advertised")
e.send("isready")
lines, _ = e.wait_for("readyok", 10)
check(lines and lines[-1] == "readyok", "isready -> readyok")

print("== perft (movegen + make/unmake, known node counts)")
SUITE = [  # (fen, depth, expected)  - standard perft positions
    ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281),
    ("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862),   # Kiwipete: castling, ep, pins
    ("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238),                              # ep + discovered checks
    ("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467),          # promotions
]
for fen, d, exp in SUITE:
    e.send(f"position fen {fen}")
    got = perft_total(e, d)
    check(got == exp, f"perft({d}) {fen.split()[0][:22]}... = {exp}", f"got {got}")

print("== position startpos moves / promotion")
e.send("position startpos moves e2e4 e7e5 g1f3")
lines, ms = go(e, "go depth 3")
check(bestmove(lines) not in (None, "0000"), "bestmove after 'position startpos moves ...'", str(lines[-1:]))
check(any(l.startswith("info depth 3 ") for l in lines), "info depth 3 printed")
inf = [l for l in lines if l.startswith("info depth")]
check(all(k in inf[-1] for k in ("score cp", "nodes", "nps", "time", "pv")), "info line has score/nodes/nps/time/pv", inf[-1] if inf else "")
e.send("position fen 8/P6k/8/8/8/8/8/K7 w - - 0 1 moves a7a8q")
check(perft_total(e, 1) == 3, "promotion move 'a7a8q' parsed (black then has exactly 3 moves)")

print("== go parameters")
e.send("position startpos")
lines, ms = go(e, "go depth 4")
check(any(l.startswith("info depth 4 ") for l in lines) and not any(l.startswith("info depth 5 ") for l in lines), "go depth 4 stops at depth 4")
lines, ms = go(e, "go movetime 600")
check(ms is not None and 400 <= ms <= 700, "go movetime 600 answers in 400-700 ms", f"{ms}")
lines, ms = go(e, "go wtime 3000 btime 3000 winc 0 binc 0")
check(ms is not None and ms <= 350, "go wtime 3000 (mtg 30 => hard limit ~270 ms) answers <= 350 ms", f"{ms}")
lines, ms = go(e, "go wtime 40 btime 40")
check(ms is not None and ms <= 200 and bestmove(lines) not in (None, "0000"), "go with almost no time still returns a legal move fast", f"{ms} {lines[-1:]}")

print("== stop / infinite")
e.send("go infinite")
time.sleep(0.4)
e.send("isready")
lines, _ = e.wait_for("readyok", 2)
check(lines and lines[-1] == "readyok", "isready answered WHILE searching")
check(not any(l.startswith("bestmove") for l in lines), "no bestmove before 'stop' under go infinite")
t0 = time.monotonic()
e.send("stop")
lines, t = e.wait_for("bestmove", 3)
check(t is not None and (t - t0) < 0.25, "stop -> bestmove within 250 ms", f"{None if t is None else (t - t0) * 1000}")
e.send("go depth 30")
time.sleep(0.2)
e.send("stop")
lines, t = e.wait_for("bestmove", 3)
check(t is not None and bestmove(lines) not in (None, "0000"), "stop during a deep search yields a legal bestmove")

print("== ucinewgame clears the transposition table")
def nodes_at_depth(depth):
    e.send("position startpos")
    lines, _ = go(e, f"go depth {depth}")
    return int([l for l in lines if l.startswith(f"info depth {depth} ")][-1].split("nodes ")[1].split()[0])
e.send("ucinewgame")               # earlier tests already filled the TT: start from a known-empty one
n_cold = nodes_at_depth(6)
n_warm = nodes_at_depth(6)         # TT still full from the previous search
e.send("ucinewgame")
n_reset = nodes_at_depth(6)
check(n_warm < n_cold, "warm TT searches fewer nodes (so the TT really persists)", f"cold={n_cold} warm={n_warm}")
check(n_reset == n_cold, "after ucinewgame the node count equals the cold run", f"cold={n_cold} reset={n_reset}")

print("== no legal moves")
e.send("position fen rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3")  # fool's mate: white is checkmated
lines, _ = go(e, "go depth 3")
check(bestmove(lines) == "0000", "checkmate -> bestmove 0000", str(lines[-1:]))
e.send("position fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")                                  # stalemate
lines, _ = go(e, "go depth 3")
check(bestmove(lines) == "0000", "stalemate -> bestmove 0000", str(lines[-1:]))

print("== finds a mate in one")
e.send("position fen 6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1")
lines, _ = go(e, "go depth 1")
check(bestmove(lines) == "a1a8" and any("score mate 1 " in line for line in lines),
      "depth 1 recognizes back-rank mate a1a8 at the search horizon", str(lines[-3:]))
e.send("position fen 4k3/8/8/8/8/8/8/R5K1 w - - 0 1")
lines, _ = go(e, "go depth 1")
check(any("score cp " in line for line in lines) and not any("score mate " in line for line in lines),
      "a check with a quiet king escape is scored as a playable position", str(lines[-3:]))

print("== ML switch")
e.send("setoption name UseML value true")
e.send("position startpos")
lines, _ = go(e, "go depth 2")
check(any("ML requested but NO priors" in l or "ML priors ok" in l for l in lines), "UseML=true reports its ML status (never silent)", str(lines[:2]))
e.send("setoption name UseML value false")
lines, _ = go(e, "go depth 2")
check(any(l == "info string ML off" for l in lines), "UseML=false reports 'ML off'")
check(e.quit() == 0, "quit exits cleanly (code 0, no hang, no crash)")

print("== ML bridge (fake model fixture: GIL, search thread, lazy loading, pipelined input)")
import tempfile
FAKE_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_ml")
with tempfile.TemporaryDirectory() as tmp:
    def fake(extra=None):
        env = {"CAISSA_ROOT": FAKE_ROOT, "FAKE_ML_DIR": tmp, "PYTHONUNBUFFERED": "1"}  # the nasty one, on purpose
        env.update(extra or {})
        return Engine(env)
    # (a) ML off from the start: Python must never even be imported
    e = fake({"CAISSA_ML": "0"})
    e.send("isready"); e.send("position startpos"); e.send("go depth 2")
    lines, _ = e.wait_for("bestmove", 10)
    check(not os.path.exists(os.path.join(tmp, "imported")), "ML off => the Python module is never imported (lazy bridge)")
    e.quit()
    # (b) ML on, input PIPELINED in one burst (must not lose lines), called from the search thread
    e = fake()
    e.p.stdin.write("uci\nisready\nposition startpos\ngo depth 3\n"); e.p.stdin.flush()
    lines, t = e.wait_for("bestmove", 15)
    check(t is not None, "pipelined isready/position/go with PYTHONUNBUFFERED=1 still produces a bestmove", str(lines[-3:]))
    check(any("ML priors ok: 2 moves" in l for l in lines), "ML on: priors received through the bridge", str(lines[-4:]))
    calls = open(os.path.join(tmp, "calls")).read().split() if os.path.exists(os.path.join(tmp, "calls")) else []
    check(calls and all(c != "MainThread" for c in calls), "Python is entered from the search thread (GIL handoff works)", str(calls))
    # (c) switch ML off at runtime: no new Python calls
    n_before = len(calls)
    e.send("setoption name UseML value false")
    lines, _ = go(e, "go depth 2")
    n_after = len(open(os.path.join(tmp, "calls")).read().split())
    check(n_after == n_before and any(l == "info string ML off" for l in lines), "setoption UseML=false => model no longer called")
    e.send("setoption name UseML value true")
    lines, _ = go(e, "go depth 2")
    n_after2 = len(open(os.path.join(tmp, "calls")).read().split())
    check(n_after2 == n_before + 1, "setoption UseML=true => model called again (exactly once per go)")
    # (d) ML latency is charged to the move clock
    t0 = time.monotonic()
    lines, ms = go(e, "go movetime 300")
    check(ms is not None and ms < 400, "go movetime 300 with a 200 ms model still answers in time", f"{ms}")
    check(e.quit() == 0, "clean exit with the interpreter running (GIL restored, Py_Finalize ok)")
    # (e) works from any working directory (cutechess starts engines wherever it wants)
    e = fake(); e.send("isready"); e.send("position startpos"); e.send("go depth 1")
    lines, t = e.wait_for("bestmove", 15)
    check(any("ML priors ok" in l for l in lines), "ML found via CAISSA_ROOT regardless of cwd")
    e.quit()

    # ML is called only at the root even when root reductions are enabled.
    e = fake({"FAKE_ML_DELAY": "0"})
    e.send("setoption name MLRootLMR value true")
    e.send("uci")
    options, _ = e.wait_for("uciok")
    check(not any("option name MLPlyOne" in line for line in options),
          "removed ply-one option is not advertised")
    n_before = len(open(os.path.join(tmp, "calls")).read().split())
    e.send("position startpos")
    lines, _ = go(e, "go depth 3")
    stats = next((line.split() for line in lines if line.startswith("info string phase4 ")), [])
    n_after = len(open(os.path.join(tmp, "calls")).read().split())
    check(n_after == n_before + 1 and len(stats) == 7 and stats[4] == "0",
          "one root inference across all iterations, no reductions below depth 4", str(stats))
    n_before = len(open(os.path.join(tmp, "calls")).read().split())
    e.send("setoption name UseML value false")
    lines, _ = go(e, "go depth 3")
    n_after = len(open(os.path.join(tmp, "calls")).read().split())
    check(n_before == n_after and "info string ML off" in lines,
          "UseML=false disables inference with root reductions selected")
    e.quit()

try:
    import chess
    print("== cross-check 'position ... moves' + movegen against python-chess on random games")
    e = Engine({"CAISSA_ML": "0"})
    random.seed(7)
    bad = 0
    for g in range(25):
        b = chess.Board()
        moves = []
        for ply in range(random.randint(10, 90)):
            ms_ = list(b.legal_moves)
            if not ms_:
                break
            m = random.choice(ms_)
            moves.append(m.uci())
            b.push(m)
        e.send("position startpos moves " + " ".join(moves))
        e.send("go perft 1")
        lines, _ = e.wait_for("nodes searched")
        ours = sorted(l.split(":")[0] for l in lines if ":" in l and not l.startswith("nodes"))
        theirs = sorted(m.uci() for m in b.legal_moves)
        if ours != theirs:
            bad += 1
            print("    mismatch after:", " ".join(moves[-8:]), "\n      ours-only:", set(ours) - set(theirs), " theirs-only:", set(theirs) - set(ours))
    check(bad == 0, "legal move lists identical to python-chess in 25 random games")
    e.quit()
except ImportError:
    print("== (python-chess not installed: skipping random-game cross-check)")

print()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED: {failures}")
sys.exit(1 if failures else 0)
