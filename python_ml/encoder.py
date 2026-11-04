import chess
import numpy as np

def fen_to_tensor(fen):
    """
    Converts a FEN string to a (14, 8, 8) spatial PyTorch tensor.
    Channels 0-5: White P,N,B,R,Q,K
    Channels 6-11: Black p,n,b,r,q,k
    Channel 12: Active Color (1 for White, 0 for Black)
    Channel 13: Castling rights
    """
    board = chess.Board(fen)
    tensor = np.zeros((14, 8, 8), dtype=np.float32)
    
    # Piece mappings
    piece_map = {
        chess.PAWN: 0, chess.KNIGHT: 1, chess.BISHOP: 2, 
        chess.ROOK: 3, chess.QUEEN: 4, chess.KING: 5
    }
    
    for sq in chess.SQUARES:
        piece = board.piece_at(sq)
        if piece:
            channel = piece_map[piece.piece_type]
            if piece.color == chess.BLACK:
                channel += 6
            r = sq // 8
            c = sq % 8
            tensor[channel][r][c] = 1.0
            
    # Active Color
    if board.turn == chess.WHITE:
        tensor[12].fill(1.0)
        
    # Castling
    if board.has_kingside_castling_rights(chess.WHITE): tensor[13][0][7] = 1.0
    if board.has_queenside_castling_rights(chess.WHITE): tensor[13][0][0] = 1.0
    if board.has_kingside_castling_rights(chess.BLACK): tensor[13][7][7] = 1.0
    if board.has_queenside_castling_rights(chess.BLACK): tensor[13][7][0] = 1.0
    
    return tensor