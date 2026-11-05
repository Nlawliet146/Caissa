import torch
import torch.nn as nn
import torch.nn.functional as F


class ResBlock(nn.Module):
    """conv -> BN -> ReLU -> conv -> BN, then ADD the input back (skip connection) and ReLU."""

    def __init__(self, channels: int):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, kernel_size=3, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, kernel_size=3, padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(channels)

    def forward(self, x):
        out = F.relu(self.bn1(self.conv1(x)))
        out = self.bn2(self.conv2(out))
        return F.relu(out + x)


class MoveOrderingNet(nn.Module):
    """
    Input : (Batch, 14, 8, 8)   board planes from encoder.py
    Output: (Batch, 4096)       RAW LOGITS, one per (from_square * 64 + to_square)

    Raw logits (not probabilities) are what the training loss expects.
    inference_pipeline.py turns them into probabilities with softmax.
    """

    def __init__(self, channels: int = 64, blocks: int = 3):
        super().__init__()
        self.conv_in = nn.Conv2d(14, channels, kernel_size=3, padding=1, bias=False)
        self.bn_in = nn.BatchNorm2d(channels)
        self.tower = nn.Sequential(*[ResBlock(channels) for _ in range(blocks)])
        # Policy head: 64 -> 32 channels (1x1), flatten 32*8*8 = 2048, Linear -> 4096
        self.head_conv = nn.Conv2d(channels, 32, kernel_size=1, bias=False)
        self.head_bn = nn.BatchNorm2d(32)
        self.fc = nn.Linear(32 * 8 * 8, 4096)

    def forward(self, x):
        x = F.relu(self.bn_in(self.conv_in(x)))
        x = self.tower(x)
        x = F.relu(self.head_bn(self.head_conv(x)))
        x = x.flatten(1)
        return self.fc(x)