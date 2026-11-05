"""
dataset.py  --  turns chess positions into the numbers the neural network eats.

Storing every position as a (14, 8, 8) float tensor would need ~3.5 KB each
(2 million positions = 7 GB of RAM).  Instead we store each position COMPACTLY
and expand it into a tensor only when a batch is needed:

    bb    12 x uint64   piece bitboards: white P N B R Q K, then black p n b r q k
                        (bit i of a bitboard = square i, with a1 = 0 ... h8 = 63,
                         exactly the same numbering as the C++ engine)
    meta  1 x uint8     bit0 = white to move, bit1 = white O-O, bit2 = white O-O-O,
                        bit3 = black O-O, bit4 = black O-O-O
    move  1 x uint16    the move a human played:  from_square * 64 + to_square
    game  1 x uint32    which game the position came from (used to split train / val)

records_to_tensor() MUST produce exactly what encoder.fen_to_tensor() produces,
otherwise the network trains on one thing and is asked about another at play time.
Run  `python python_ml/dataset.py`  to check this (a self-test at the bottom).
"""

import os

import chess
import numpy as np
import torch

_SHIFTS = torch.arange(64, dtype=torch.int64)


def board_to_record(board: chess.Board):
    """chess.Board -> (list of 12 python ints, meta int)."""
    bbs = [
        board.pieces(piece_type, color).mask
        for color in (chess.WHITE, chess.BLACK)
        for piece_type in range(1, 7)  # PAWN..KING
    ]
    meta = int(board.turn == chess.WHITE)
    meta |= int(board.has_kingside_castling_rights(chess.WHITE)) << 1
    meta |= int(board.has_queenside_castling_rights(chess.WHITE)) << 2
    meta |= int(board.has_kingside_castling_rights(chess.BLACK)) << 3
    meta |= int(board.has_queenside_castling_rights(chess.BLACK)) << 4
    return bbs, meta


def records_to_tensor(bb: torch.Tensor, meta: torch.Tensor) -> torch.Tensor:
    """
    bb   : (B, 12) int64   (the uint64 bit patterns, viewed as int64)
    meta : (B,)    int64
    returns (B, 14, 8, 8) float32, identical to encoder.fen_to_tensor().
    """
    batch = bb.shape[0]
    bits = (bb.unsqueeze(-1) >> _SHIFTS) & 1            # (B, 12, 64)
    out = torch.zeros(batch, 14, 8, 8, dtype=torch.float32)
    out[:, :12] = bits.to(torch.float32).view(batch, 12, 8, 8)
    out[:, 12] = (meta & 1).to(torch.float32).view(batch, 1, 1)
    out[:, 13, 0, 7] = ((meta >> 1) & 1).to(torch.float32)  # white king-side
    out[:, 13, 0, 0] = ((meta >> 2) & 1).to(torch.float32)  # white queen-side
    out[:, 13, 7, 7] = ((meta >> 3) & 1).to(torch.float32)  # black king-side
    out[:, 13, 7, 0] = ((meta >> 4) & 1).to(torch.float32)  # black queen-side
    return out


class PositionStore:
    """Loads the .npy files written by prepare_data.py and hands out batches."""

    def __init__(self, folder: str):
        self.bb = np.load(os.path.join(folder, "bb.npy"))
        self.meta = np.load(os.path.join(folder, "meta.npy"))
        self.move = np.load(os.path.join(folder, "move.npy"))
        self.game = np.load(os.path.join(folder, "game.npy"))

    def __len__(self):
        return len(self.move)

    def batch(self, idx: np.ndarray):
        """idx: array of row numbers -> (inputs (B,14,8,8) float, labels (B,) long)."""
        bb = torch.from_numpy(self.bb[idx].view(np.int64))
        meta = torch.from_numpy(self.meta[idx].astype(np.int64))
        x = records_to_tensor(bb, meta)
        y = torch.from_numpy(self.move[idx].astype(np.int64))
        return x, y


def _selftest():
    import random

    from encoder import fen_to_tensor

    random.seed(0)
    boards_checked = 0
    for _ in range(60):
        board = chess.Board()
        for _ply in range(random.randint(0, 120)):
            moves = list(board.legal_moves)
            if not moves:
                break
            board.push(random.choice(moves))
            bbs, meta = board_to_record(board)
            arr = np.array(bbs, dtype=np.uint64).view(np.int64).reshape(1, 12)
            mine = records_to_tensor(torch.from_numpy(arr), torch.tensor([meta]))[0].numpy()
            theirs = fen_to_tensor(board.fen())
            if not np.array_equal(mine, theirs):
                raise SystemExit("MISMATCH on FEN: " + board.fen())
            boards_checked += 1
    print(f"OK: dataset encoding matches encoder.py on {boards_checked} positions.")


if __name__ == "__main__":
    _selftest()