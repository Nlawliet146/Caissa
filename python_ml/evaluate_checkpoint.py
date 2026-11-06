"""Evaluate the shipped checkpoint on the training script's held-back games."""
import argparse
from pathlib import Path

import numpy as np
import torch

from checkpoint import load_policy
from dataset import PositionStore
from move_ordering_model import MoveOrderingNet
from train_move_ordering import evaluate

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", default=ROOT / "data/processed")
    parser.add_argument("--weights", default=ROOT / "weights/move_ordering_net.pt")
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args()
    if min(args.batch_size, args.threads) <= 0:
        parser.error("batch size and thread count must be positive")
    torch.set_num_threads(args.threads)
    store = PositionStore(args.data)
    indices = np.flatnonzero(store.game % 20 == 0)[:50_000]
    if not len(indices):
        parser.error("the dataset has no validation positions")
    model = MoveOrderingNet()
    weights, policy_format = load_policy(args.weights)
    if policy_format != store.policy_format:
        parser.error("checkpoint and dataset use different board encodings")
    model.load_state_dict(weights)
    loss, top1, top5 = evaluate(model, store, indices, args.batch_size, "cpu")
    print(f"Validation: {len(indices):,} positions from {len(np.unique(store.game[indices])):,} games")
    mode = "Legal-masked" if policy_format else "Unmasked"
    print(f"{mode} cross-entropy: {loss:.4f} | top-1: {top1:.2%} | top-5: {top5:.2%}")


if __name__ == "__main__":
    main()
