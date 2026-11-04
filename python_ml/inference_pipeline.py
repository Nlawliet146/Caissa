import torch
import os
from encoder import fen_to_tensor
from move_ordering_model import MoveOrderingNet

# Initialize model once into memory
model = MoveOrderingNet()
model.eval()

# Check if weights exist (Graceful degradation if Pratham hasn't trained it yet)
weight_path = "../weights/move_ordering_net.pt"
if os.path.exists(weight_path):
    model.load_state_dict(torch.load(weight_path, map_location='cpu'))

def get_search_priors(fen_string):
    """
    Called strictly once per move from C++ (pybind11).
    Takes a FEN string, runs PyTorch inference, returns a list of 4096 floats.
    """
    try:
        # Convert to tensor and add Batch dimension
        tensor = fen_to_tensor(fen_string)
        inputs = torch.tensor(tensor).unsqueeze(0)
        
        with torch.no_grad():
            output = model(inputs)
            
        # Flatten back to a standard Python list for pybind11 translation
        priors_list = output.squeeze().tolist()
        return priors_list
    except Exception as e:
        # Failsafe: Return flat prior so C++ doesn't crash
        return [0.0] * 4096