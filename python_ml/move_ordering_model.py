import torch
import torch.nn as nn
import torch.nn.functional as F

class MoveOrderingNet(nn.Module):
    def __init__(self):
        super().__init__()
        # Input: 14 channels
        self.conv1 = nn.Conv2d(14, 64, kernel_size=3, padding=1)
        self.bn1 = nn.BatchNorm2d(64)
        
        self.conv2 = nn.Conv2d(64, 64, kernel_size=3, padding=1)
        self.bn2 = nn.BatchNorm2d(64)
        
        self.conv3 = nn.Conv2d(64, 64, kernel_size=3, padding=1)
        self.bn3 = nn.BatchNorm2d(64)
        
        # Output Head: 4096 logits (64 from_sq * 64 to_sq)
        self.fc = nn.Linear(64 * 8 * 8, 4096)

    def forward(self, x):
        x = F.relu(self.bn1(self.conv1(x)))
        x = F.relu(self.bn2(self.conv2(x)))
        x = F.relu(self.bn3(self.conv3(x)))
        
        x = x.view(x.size(0), -1) # Flatten
        x = self.fc(x)
        return torch.sigmoid(x) # Output probabilities between 0.0 and 1.0