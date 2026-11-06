"""
prepare_data.py  --  PGN game file  ->  training positions.

For every move a strong human played in a game we save one training example:
    (position BEFORE the move,  the move that was played)

Usage (from the project root):
    python python_ml/prepare_data.py --pgn data/raw/games.pgn --max-games 20000
    python python_ml/prepare_data.py --pgn data/raw/lichess_db_standard_rated_2013-01.pgn.zst --min-elo 2000

Accepts plain .pgn, .pgn.zst (Lichess dumps) and .zip files.
Press Ctrl+C at any time: what has been collected so far is still saved.
"""
import argparse
from array import array
import io
import os
import re
import time
import zipfile

import chess
import chess.pgn
import numpy as np

from dataset import board_to_record
from encoder import mirror_position, move_index

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RE_WHITE_ELO = re.compile(r'\[WhiteElo "(\d+)"\]')
RE_BLACK_ELO = re.compile(r'\[BlackElo "(\d+)"\]')
RE_EVENT = re.compile(r'\[Event "([^"]*)"\]')


def open_text(path: str):
    """Open .pgn / .pgn.zst / .zip as a stream of text lines."""
    if path.endswith(".zst"):
        import zstandard

        raw = open(path, "rb")
        reader = zstandard.ZstdDecompressor().stream_reader(raw)
        return io.TextIOWrapper(reader, encoding="utf-8", errors="replace")
    if path.endswith(".zip"):
        zf = zipfile.ZipFile(path)
        name = [n for n in zf.namelist() if n.lower().endswith(".pgn")][0]
        return io.TextIOWrapper(zf.open(name), encoding="utf-8", errors="replace")
    return open(path, "r", encoding="utf-8", errors="replace")


def iter_game_texts(handle):
    """Split a PGN stream into one text block per game (cheap: no chess parsing)."""
    buf, seen_moves = [], False
    for line in handle:
        s = line.strip()
        if not s:
            if seen_moves:
                yield "".join(buf)
                buf, seen_moves = [], False
            else:
                buf.append(line)
            continue
        if s.startswith("["):
            if seen_moves:  # a new game started without a blank line
                yield "".join(buf)
                buf, seen_moves = [], False
            buf.append(line)
        else:
            seen_moves = True
            buf.append(line)
    if buf and seen_moves:
        yield "".join(buf)


def wanted(text: str, min_elo: int) -> bool:
    w, b = RE_WHITE_ELO.search(text), RE_BLACK_ELO.search(text)
    if not (w and b):
        return False
    if int(w.group(1)) < min_elo or int(b.group(1)) < min_elo:
        return False
    ev = RE_EVENT.search(text)
    if ev and "bullet" in ev.group(1).lower():  # bullet games = lots of panic moves
        return False
    return True


def save(out_dir, chunks, phase5):
    os.makedirs(out_dir, exist_ok=True)
    if not chunks:
        raise SystemExit("No positions collected - check the file path and --min-elo.")
    bb = np.concatenate([c[0] for c in chunks])
    meta = np.concatenate([c[1] for c in chunks])
    move = np.concatenate([c[2] for c in chunks])
    game = np.concatenate([c[3] for c in chunks])
    np.save(os.path.join(out_dir, "bb.npy"), bb)
    np.save(os.path.join(out_dir, "meta.npy"), meta)
    np.save(os.path.join(out_dir, "move.npy"), move)
    np.save(os.path.join(out_dir, "game.npy"), game)
    if phase5:
        np.save(os.path.join(out_dir, "legal.npy"), np.concatenate([c[4] for c in chunks]))
        counts = np.concatenate([c[5] for c in chunks])
        offsets = np.empty(len(counts) + 1, dtype=np.int64)
        offsets[0] = 0
        np.cumsum(counts, out=offsets[1:])
        np.save(os.path.join(out_dir, "legal_offsets.npy"), offsets)
    print(f"Saved {len(move):,} positions from {int(game.max()) + 1:,} games -> {out_dir}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pgn", required=True, help="path to .pgn / .pgn.zst / .zip")
    ap.add_argument("--out", help="output directory (must be new for --phase5)")
    ap.add_argument("--phase5", action="store_true", help="normalize perspective and save legal labels")
    ap.add_argument("--min-elo", type=int, default=2000, help="BOTH players must be at least this")
    ap.add_argument("--max-games", type=int, default=20000, help="stop after this many accepted games")
    args = ap.parse_args()

    if args.max_games <= 0:
        ap.error("--max-games must be positive")
    if args.out is None:
        args.out = os.path.join(ROOT, "data", "phase5" if args.phase5 else "processed")
    if os.path.exists(args.out) and (args.phase5 or os.path.exists(os.path.join(args.out, "legal_offsets.npy"))):
        ap.error("choose a fresh output directory for Phase 5 data")
    legal, counts = array("H"), array("H")
    chunks, bb_l, meta_l, move_l, game_l = [], [], [], [], []

    def flush():
        if move_l:
            chunks.append((
                np.array(bb_l, dtype=np.uint64).reshape(-1, 12),
                np.array(meta_l, dtype=np.uint8),
                np.array(move_l, dtype=np.uint16),
                np.array(game_l, dtype=np.uint32),
                np.array(legal, dtype=np.uint16),
                np.array(counts, dtype=np.uint16),
            ))
            bb_l.clear()
            meta_l.clear()
            move_l.clear()
            game_l.clear()
            del legal[:]
            del counts[:]

    accepted, seen, start = 0, 0, time.time()
    try:
        with open_text(args.pgn) as handle:
            for text in iter_game_texts(handle):
                seen += 1
                if not wanted(text, args.min_elo):
                    continue
                game = chess.pgn.read_game(io.StringIO(text))
                if game is None or game.errors:
                    continue
                board = game.board()
                n_before = len(move_l)
                for move in game.mainline_moves():
                    mirrored = args.phase5 and board.turn == chess.BLACK
                    bbs, meta = board_to_record(mirror_position(board) if mirrored else board)
                    if args.phase5:
                        labels = {move_index(candidate, mirrored) for candidate in board.legal_moves}
                        legal.extend(sorted(labels))
                        counts.append(len(labels))
                    bb_l.extend(bbs)
                    meta_l.append(meta)
                    move_l.append(move_index(move, mirrored))
                    game_l.append(accepted)
                    board.push(move)
                if len(move_l) == n_before:
                    continue
                accepted += 1
                if len(move_l) >= 200_000:
                    flush()
                if accepted % 500 == 0:
                    rate = accepted / (time.time() - start)
                    print(f"  {accepted:,} games kept (looked at {seen:,}) - {rate:.0f} games/s", flush=True)
                if accepted >= args.max_games:
                    break
    except KeyboardInterrupt:
        print("\nStopped by you - saving what we have so far...")
    flush()
    save(args.out, chunks, args.phase5)


if __name__ == "__main__":
    main()