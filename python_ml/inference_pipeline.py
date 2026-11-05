import os
import sys

import chess
import torch

from encoder import fen_to_tensor
from move_ordering_model import MoveOrderingNet

# NOTE: inside the engine, stdout is the UCI channel to the chess GUI.
# Never print() normally here - diagnostics go to stderr.

torch.set_num_threads(1)  # one tiny forward pass per move: threads only add overhead

# Resolve the weights file relative to THIS file, not the current directory,
# so it works no matter where ./build/caissa is launched from.
HERE = os.path.dirname(os.path.abspath(__file__))
WEIGHT_PATH = os.path.normpath(os.path.join(HERE, "..", "weights", "move_ordering_net.pt"))

model = MoveOrderingNet()
model.eval()

weights_loaded = False
if os.path.exists(WEIGHT_PATH):
    try:
        model.load_state_dict(torch.load(WEIGHT_PATH, map_location="cpu"))
        weights_loaded = True
        print(f"[Caissa ML] Loaded weights: {WEIGHT_PATH}", file=sys.stderr)
    except Exception as e:  # wrong architecture, corrupted file, ...
        print(f"[Caissa ML] Could not load weights ({e}). Using plain C++ ordering.", file=sys.stderr)
else:
    print(f"[Caissa ML] No weights at {WEIGHT_PATH}. Using plain C++ ordering.", file=sys.stderr)

FLAT = [0.0] * 4096


def get_search_priors(fen_string):
    """
    Called once per move from C++ (pybind11).
    Returns 4096 floats: the probability the network gives each (from*64+to) move.
    Illegal moves get exactly 0 and the legal ones sum to 1.
    If no trained weights are loaded we return all zeros: an UNTRAINED network
    would only add random noise to the move ordering.
    """
    if not weights_loaded:
        return FLAT
    try:
        board = chess.Board(fen_string)
        legal = [m.from_square * 64 + m.to_square for m in board.legal_moves]
        if not legal:
            return FLAT

        x = torch.from_numpy(fen_to_tensor(fen_string)).unsqueeze(0)
        with torch.inference_mode():
            logits = model(x)[0]

        mask = torch.full((4096,), float("-inf"))
        mask[legal] = 0.0
        return torch.softmax(logits + mask, dim=0).tolist()
    except Exception as e:
        print(f"[Caissa ML] inference failed: {e}", file=sys.stderr)
        return FLAT


# Warm-up so the first real move is not slower than the rest.
try:
    get_search_priors(chess.STARTING_FEN)
except Exception:
    pass