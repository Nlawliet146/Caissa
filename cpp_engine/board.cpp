#include "board.hpp"
#include "movegen.hpp"
#include <sstream>
#include <cctype>
#include <cstring>

Board::Board() {
    clear();
}

void Board::clear() {
    std::memset(piece_bitboards, 0, sizeof(piece_bitboards));
    std::memset(color_bitboards, 0, sizeof(color_bitboards));
    for (int i = 0; i < 64; i++) square_board[i] = EMPTY;
    side_to_move = WHITE;
    castling_rights = 0;
    en_passant_sq = NO_SQ;
    halfmove_clock = 0;
    fullmove_number = 1;
    history.clear();
}

void Board::parse_fen(const std::string& fen) {
    clear();
    std::istringstream ss(fen);
    std::string pos, color, castling, ep;
    ss >> pos >> color >> castling >> ep;

    int rank = 7, file = 0;
    for (char c : pos) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (std::isdigit(c)) {
            file += (c - '0');
        } else {
            int sq = rank * 8 + file;
            int piece = EMPTY;
            switch (c) {
                case 'P': piece = W_PAWN; break;
                case 'N': piece = W_KNIGHT; break;
                case 'B': piece = W_BISHOP; break;
                case 'R': piece = W_ROOK; break;
                case 'Q': piece = W_QUEEN; break;
                case 'K': piece = W_KING; break;
                case 'p': piece = B_PAWN; break;
                case 'n': piece = B_KNIGHT; break;
                case 'b': piece = B_BISHOP; break;
                case 'r': piece = B_ROOK; break;
                case 'q': piece = B_QUEEN; break;
                case 'k': piece = B_KING; break;
            }
            if (piece != EMPTY) {
                piece_bitboards[piece] |= (1ULL << sq);
                square_board[sq] = piece;
                int col = (piece <= W_KING) ? WHITE : BLACK;
                color_bitboards[col] |= (1ULL << sq);
            }
            file++;
        }
    }

    color_bitboards[BOTH] = color_bitboards[WHITE] | color_bitboards[BLACK];
    side_to_move = (color == "w") ? WHITE : BLACK;

    if (castling.find('K') != std::string::npos) castling_rights |= WK;
    if (castling.find('Q') != std::string::npos) castling_rights |= WQ;
    if (castling.find('k') != std::string::npos) castling_rights |= BK;
    if (castling.find('q') != std::string::npos) castling_rights |= BQ;

    if (ep != "-") {
        int f = ep[0] - 'a';
        int r = ep[1] - '1';
        en_passant_sq = r * 8 + f;
    }
}

std::string Board::to_fen() const {
    std::string fen = "";
    for (int rank = 7; rank >= 0; rank--) {
        int empty_cnt = 0;
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            int p = square_board[sq];
            if (p == EMPTY) {
                empty_cnt++;
            } else {
                if (empty_cnt > 0) { fen += std::to_string(empty_cnt); empty_cnt = 0; }
                const char p_chars[] = "PNBRQKpnbrqk";
                fen += p_chars[p];
            }
        }
        if (empty_cnt > 0) fen += std::to_string(empty_cnt);
        if (rank > 0) fen += "/";
    }

    fen += (side_to_move == WHITE) ? " w " : " b ";
    std::string c_str = "";
    if (castling_rights & WK) c_str += "K";
    if (castling_rights & WQ) c_str += "Q";
    if (castling_rights & BK) c_str += "k";
    if (castling_rights & BQ) c_str += "q";
    fen += c_str.empty() ? "-" : c_str;

    if (en_passant_sq != NO_SQ) {
        char f = 'a' + (en_passant_sq % 8);
        char r = '1' + (en_passant_sq / 8);
        fen += std::string(" ") + f + r;
    } else {
        fen += " -";
    }

    fen += " 0 1";
    return fen;
}

std::string move_to_uci(const Move& m) {
    if (m.from_sq >= 64 || m.to_sq >= 64) return "0000";
    std::string s;
    s += static_cast<char>('a' + m.from_sq % 8);
    s += static_cast<char>('1' + m.from_sq / 8);
    s += static_cast<char>('a' + m.to_sq % 8);
    s += static_cast<char>('1' + m.to_sq / 8);
    switch (m.promoted_piece) {
        case W_QUEEN:  case B_QUEEN:  s += 'q'; break;
        case W_ROOK:   case B_ROOK:   s += 'r'; break;
        case W_BISHOP: case B_BISHOP: s += 'b'; break;
        case W_KNIGHT: case B_KNIGHT: s += 'n'; break;
        default: break; // EMPTY: not a promotion
    }
    return s;
}

bool Board::make_move(const Move& m) {
    UndoState state;
    state.castling_rights = castling_rights;
    state.en_passant_sq = en_passant_sq;
    state.halfmove_clock = halfmove_clock;
    state.captured_piece = square_board[m.to_sq];
    history.push_back(state);

    int moving_piece = square_board[m.from_sq];
    int us = side_to_move;
    int them = ~us & 1;

    // Remove moving piece from source square
    piece_bitboards[moving_piece] &= ~(1ULL << m.from_sq);
    color_bitboards[us] &= ~(1ULL << m.from_sq);
    square_board[m.from_sq] = EMPTY;

    // Handle captures
    if (m.flags == 1 && state.captured_piece != EMPTY) {
        piece_bitboards[state.captured_piece] &= ~(1ULL << m.to_sq);
        color_bitboards[them] &= ~(1ULL << m.to_sq);
    } else if (m.flags == 2) { // En Passant capture
        int ep_cap_sq = (us == WHITE) ? m.to_sq - 8 : m.to_sq + 8;
        int ep_piece = (us == WHITE) ? B_PAWN : W_PAWN;
        piece_bitboards[ep_piece] &= ~(1ULL << ep_cap_sq);
        color_bitboards[them] &= ~(1ULL << ep_cap_sq);
        square_board[ep_cap_sq] = EMPTY;
    }

    // Handle Promotions
    int dest_piece = moving_piece;
    if (m.promoted_piece != EMPTY) {
        dest_piece = m.promoted_piece;
    }

    // Place piece on target square
    piece_bitboards[dest_piece] |= (1ULL << m.to_sq);
    color_bitboards[us] |= (1ULL << m.to_sq);
    square_board[m.to_sq] = dest_piece;

    // Handle Castling rook move
    if (m.flags == 3) {
        int r_from = NO_SQ, r_to = NO_SQ, r_piece = (us == WHITE) ? W_ROOK : B_ROOK;
        if (m.to_sq == G1) { r_from = H1; r_to = F1; }
        else if (m.to_sq == C1) { r_from = A1; r_to = D1; }
        else if (m.to_sq == G8) { r_from = H8; r_to = F8; }
        else if (m.to_sq == C8) { r_from = A8; r_to = D8; }

        piece_bitboards[r_piece] &= ~(1ULL << r_from);
        color_bitboards[us] &= ~(1ULL << r_from);
        square_board[r_from] = EMPTY;

        piece_bitboards[r_piece] |= (1ULL << r_to);
        color_bitboards[us] |= (1ULL << r_to);
        square_board[r_to] = r_piece;
    }

    // Update En Passant Target
    if ((moving_piece == W_PAWN || moving_piece == B_PAWN) && std::abs(m.to_sq - m.from_sq) == 16) {
        en_passant_sq = (us == WHITE) ? m.from_sq + 8 : m.from_sq - 8;
    } else {
        en_passant_sq = NO_SQ;
    }

    // Update Castling Rights
    if (moving_piece == W_KING) castling_rights &= ~(WK | WQ);
    if (moving_piece == B_KING) castling_rights &= ~(BK | BQ);
    if (m.from_sq == H1 || m.to_sq == H1) castling_rights &= ~WK;
    if (m.from_sq == A1 || m.to_sq == A1) castling_rights &= ~WQ;
    if (m.from_sq == H8 || m.to_sq == H8) castling_rights &= ~BK;
    if (m.from_sq == A8 || m.to_sq == A8) castling_rights &= ~BQ;

    color_bitboards[BOTH] = color_bitboards[WHITE] | color_bitboards[BLACK];
    side_to_move = them;

    // Check legality (King cannot be in check)
    int king_piece = (us == WHITE) ? W_KING : B_KING;
    uint64_t k_bb = piece_bitboards[king_piece];
    if (k_bb) {
        int king_sq = __builtin_ctzll(k_bb);
        if (is_square_attacked(*this, king_sq, them)) {
            unmake_move(m);
            return false;
        }
    }

    return true;
}

void Board::unmake_move(const Move& m) {
    UndoState state = history.back();
    history.pop_back();

    side_to_move = ~side_to_move & 1;
    int us = side_to_move;
    int them = ~us & 1;

    int moved_piece = square_board[m.to_sq];
    if (m.promoted_piece != EMPTY) moved_piece = (us == WHITE) ? W_PAWN : B_PAWN;

    // Restore moving piece
    piece_bitboards[square_board[m.to_sq]] &= ~(1ULL << m.to_sq);
    color_bitboards[us] &= ~(1ULL << m.to_sq);

    piece_bitboards[moved_piece] |= (1ULL << m.from_sq);
    color_bitboards[us] |= (1ULL << m.from_sq);

    square_board[m.from_sq] = moved_piece;
    square_board[m.to_sq] = state.captured_piece;

    // Restore Captures
    if (m.flags == 1 && state.captured_piece != EMPTY) {
        piece_bitboards[state.captured_piece] |= (1ULL << m.to_sq);
        color_bitboards[them] |= (1ULL << m.to_sq);
    } else if (m.flags == 2) {
        square_board[m.to_sq] = EMPTY;
        int ep_cap_sq = (us == WHITE) ? m.to_sq - 8 : m.to_sq + 8;
        int ep_piece = (us == WHITE) ? B_PAWN : W_PAWN;
        piece_bitboards[ep_piece] |= (1ULL << ep_cap_sq);
        color_bitboards[them] |= (1ULL << ep_cap_sq);
        square_board[ep_cap_sq] = ep_piece;
    }

    // Restore Castling
    if (m.flags == 3) {
        int r_from = NO_SQ, r_to = NO_SQ, r_piece = (us == WHITE) ? W_ROOK : B_ROOK;
        if (m.to_sq == G1) { r_from = H1; r_to = F1; }
        else if (m.to_sq == C1) { r_from = A1; r_to = D1; }
        else if (m.to_sq == G8) { r_from = H8; r_to = F8; }
        else if (m.to_sq == C8) { r_from = A8; r_to = D8; }

        piece_bitboards[r_piece] &= ~(1ULL << r_to);
        color_bitboards[us] &= ~(1ULL << r_to);
        square_board[r_to] = EMPTY;

        piece_bitboards[r_piece] |= (1ULL << r_from);
        color_bitboards[us] |= (1ULL << r_from);
        square_board[r_from] = r_piece;
    }

    castling_rights = state.castling_rights;
    en_passant_sq = state.en_passant_sq;
    halfmove_clock = state.halfmove_clock;
    color_bitboards[BOTH] = color_bitboards[WHITE] | color_bitboards[BLACK];
}