"""Read both shipped weights and the versioned Phase 5 policy checkpoints."""
from pathlib import Path
import tempfile

import torch

from encoder import POLICY_FORMAT


def load_policy(path):
    checkpoint = torch.load(path, map_location="cpu", weights_only=True)
    if "model_state_dict" not in checkpoint:
        return checkpoint, None
    if checkpoint.get("policy_format") != POLICY_FORMAT:
        raise ValueError("Unsupported policy checkpoint format")
    return checkpoint["model_state_dict"], POLICY_FORMAT


def save_policy(path, checkpoint):
    """Replace an epoch snapshot only after serialization succeeds."""
    path = Path(path)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        torch.save(checkpoint, temporary_path)
        temporary_path.replace(path)
    finally:
        temporary_path.unlink(missing_ok=True)
