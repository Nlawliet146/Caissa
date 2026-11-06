"""
train_move_ordering.py  --  teach the network to predict the move a strong human plays.

Usage (from the project root, venv active):
    python python_ml/train_move_ordering.py                  # sensible defaults
    python python_ml/train_move_ordering.py --epochs 10 --batch-size 512

Reads : data/processed/*.npy         (made by prepare_data.py)
Writes: best weights by validation loss, plus the latest epoch for Phase 5 data
"""
import argparse
import os
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

from checkpoint import save_policy
from dataset import PositionStore
from move_ordering_model import MoveOrderingNet

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def policy_logits(model, store, indices, inputs):
    logits = model(inputs)
    legal = store.legal_mask(indices)
    return logits if legal is None else logits.masked_fill(~legal.to(logits.device), float("-inf"))


@torch.no_grad()
def evaluate(model, store, idx, batch_size, device):
    """Loss, top-1 and top-5 accuracy on the supplied split."""
    model.eval()
    loss_fn = nn.CrossEntropyLoss(reduction="sum")
    total_loss, top1, top5, n = 0.0, 0, 0, 0
    for start in range(0, len(idx), batch_size):
        batch = idx[start:start + batch_size]
        x, y = store.batch(batch)
        x, y = x.to(device), y.to(device)
        logits = policy_logits(model, store, batch, x)
        total_loss += loss_fn(logits, y).item()
        best5 = logits.topk(5, dim=1).indices
        top1 += (best5[:, 0] == y).sum().item()
        top5 += (best5 == y.unsqueeze(1)).any(dim=1).sum().item()
        n += len(y)
    return total_loss / n, top1 / n, top5 / n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=os.path.join(ROOT, "data", "processed"))
    ap.add_argument("--out", type=Path, help="best model path (required with Phase 5 data)")
    ap.add_argument("--epochs", type=int, default=5)
    ap.add_argument("--batch-size", type=int, default=256)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--max-train", type=int, default=0, help="use only this many training positions (0 = all)")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    if min(args.epochs, args.batch_size, args.lr) <= 0 or args.max_train < 0:
        ap.error("epochs, batch size and learning rate must be positive, and max-train nonnegative")

    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Device: {device}")

    store = PositionStore(args.data)
    shipped = Path(ROOT) / "weights/move_ordering_net.pt"
    if store.policy_format and (args.out is None or args.out.resolve() == shipped.resolve()):
        ap.error("give Phase 5 an explicit --out path separate from the shipped checkpoint")
    out = args.out or shipped
    latest = out.with_name(out.stem + ".latest" + out.suffix)
    if store.policy_format and (out.exists() or latest.exists()):
        ap.error("choose a new Phase 5 checkpoint path")
    print(f"Loaded {len(store):,} positions")

    # Split BY GAME: every 20th game (5%) is held back for validation.
    # (Splitting by position would leak: neighbouring positions of one game look alike.)
    is_val = (store.game % 20) == 0
    train_idx = np.flatnonzero(~is_val)
    val_idx = np.flatnonzero(is_val)
    if not len(train_idx) or not len(val_idx):
        raise SystemExit("Not enough games for a validation split - prepare more games.")
    if args.max_train and args.max_train < len(train_idx):
        train_idx = rng.choice(train_idx, args.max_train, replace=False)
    val_idx = val_idx[:50_000]  # plenty for a stable estimate, keeps evaluation quick
    print(f"Train: {len(train_idx):,} positions | Validation: {len(val_idx):,} positions (different games)")

    model = MoveOrderingNet().to(device)
    print(f"Model parameters: {sum(p.numel() for p in model.parameters()):,}")

    loss_fn = nn.CrossEntropyLoss()
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    steps_per_epoch = (len(train_idx) + args.batch_size - 1) // args.batch_size
    sched = torch.optim.lr_scheduler.OneCycleLR(
        opt, max_lr=args.lr, total_steps=args.epochs * steps_per_epoch
    )

    out.parent.mkdir(parents=True, exist_ok=True)
    best_val = float("inf")
    t0 = time.time()

    for epoch in range(1, args.epochs + 1):
        model.train()
        order = rng.permutation(train_idx)
        run_loss, run_hits, run_n = 0.0, 0, 0
        for step in range(steps_per_epoch):
            batch = order[step * args.batch_size:(step + 1) * args.batch_size]
            x, y = store.batch(batch)
            x, y = x.to(device), y.to(device)

            logits = policy_logits(model, store, batch, x)
            loss = loss_fn(logits, y)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            sched.step()

            run_loss += loss.item() * len(y)
            run_hits += (logits.argmax(1) == y).sum().item()
            run_n += len(y)
            if (step + 1) % 200 == 0 or step + 1 == steps_per_epoch:
                done = (epoch - 1) * steps_per_epoch + step + 1
                total = args.epochs * steps_per_epoch
                elapsed = time.time() - t0
                eta = elapsed / done * (total - done)
                print(f"  epoch {epoch}/{args.epochs}  step {step + 1}/{steps_per_epoch}  "
                      f"train loss {run_loss / run_n:.3f}  train top-1 {run_hits / run_n:.1%}  "
                      f"ETA {eta / 60:.1f} min", flush=True)
                run_loss, run_hits, run_n = 0.0, 0, 0

        v_loss, v_top1, v_top5 = evaluate(model, store, val_idx, 1024, device)
        print(f"== epoch {epoch} done | VALIDATION loss {v_loss:.3f}  top-1 {v_top1:.1%}  top-5 {v_top5:.1%}")
        if store.policy_format or v_loss < best_val:
            weights = {key: tensor.cpu() for key, tensor in model.state_dict().items()}
            checkpoint = weights
            if store.policy_format:
                checkpoint = {
                    "policy_format": store.policy_format,
                    "model_state_dict": weights,
                    "epoch": epoch,
                    "validation_loss": v_loss,
                    "seed": args.seed,
                }
                save_policy(latest, checkpoint)
            if v_loss < best_val:
                best_val = v_loss
                save_policy(out, checkpoint)
                print(f"   saved new best model -> {out}")

    print(f"Finished in {(time.time() - t0) / 60:.1f} min. Best validation loss: {best_val:.3f}")


if __name__ == "__main__":
    main()