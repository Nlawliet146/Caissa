#include "movegen.hpp"
#include <bit>

static uint64_t KnightAttacks[64];
static uint64_t KingAttacks[64];

void init_attack_tables()
{
    int k_offsets[] = {-17, -15, -10, -6, 6, 10, 15, 17};
    int king_offsets[] = {-9, -8, -7, -1, 1, 7, 8, 9};

    for (int sq = 0; sq < 64; sq++)
    {
        uint64_t k_bb = 0;
        int r = sq / 8, f = sq % 8;
        for (int off : k_offsets)
        {
            int target = sq + off;
            if (target >= 0 && target < 64)
            {
                int tr = target / 8, tf = target % 8;
                if (std::abs(r - tr) <= 2 && std::abs(f - tf) <= 2)
                    k_bb |= (1ULL << target);
            }
        }
        KnightAttacks[sq] = k_bb;

        uint64_t king_bb = 0;
        for (int off : king_offsets)
        {
            int target = sq + off;
            if (target >= 0 && target < 64)
            {
                int tr = target / 8, tf = target % 8;
                if (std::abs(r - tr) <= 1 && std::abs(f - tf) <= 1)
                    king_bb |= (1ULL << target);
            }
        }
        KingAttacks[sq] = king_bb;
    }
}

static uint64_t get_ray_attacks(int sq, int r_inc, int f_inc, uint64_t occ)
{
    uint64_t attacks = 0;
    int r = sq / 8 + r_inc;
    int f = sq % 8 + f_inc;
    while (r >= 0 && r < 8 && f >= 0 && f < 8)
    {
        int target = r * 8 + f;
        attacks |= (1ULL << target);
        if (occ & (1ULL << target))
            break;
        r += r_inc;
        f += f_inc;
    }
    return attacks;
}

bool is_square_attacked(const Board &board, int sq, int attacker_color)
{
    // Pawn Attacks
    if (attacker_color == WHITE)
    {
        if ((sq >= 8 && sq % 8 > 0) &&
            (board.piece_bitboards[W_PAWN] & (1ULL << (sq - 9))))
            return true;
        if ((sq >= 8 && sq % 8 < 7) &&
            (board.piece_bitboards[W_PAWN] & (1ULL << (sq - 7))))
            return true;
    }
    else
    {
        if ((sq <= 55 && sq % 8 > 0) &&
            (board.piece_bitboards[B_PAWN] & (1ULL << (sq + 7))))
            return true;
        if ((sq <= 55 && sq % 8 < 7) &&
            (board.piece_bitboards[B_PAWN] & (1ULL << (sq + 9))))
            return true;
    }

    // Knight Attacks
    int kn_piece = (attacker_color == WHITE) ? W_KNIGHT : B_KNIGHT;
    if (KnightAttacks[sq] & board.piece_bitboards[kn_piece])
        return true;

    // King Attacks
    int kg_piece = (attacker_color == WHITE) ? W_KING : B_KING;
    if (KingAttacks[sq] & board.piece_bitboards[kg_piece])
        return true;

    // Sliding Attacks
    uint64_t occ = board.color_bitboards[BOTH];
    uint64_t b_attacks =
        get_ray_attacks(sq, 1, 1, occ) | get_ray_attacks(sq, 1, -1, occ) |
        get_ray_attacks(sq, -1, 1, occ) | get_ray_attacks(sq, -1, -1, occ);
    int b_piece = (attacker_color == WHITE) ? W_BISHOP : B_BISHOP;
    int q_piece = (attacker_color == WHITE) ? W_QUEEN : B_QUEEN;
    if (b_attacks &
        (board.piece_bitboards[b_piece] | board.piece_bitboards[q_piece]))
        return true;

    uint64_t r_attacks =
        get_ray_attacks(sq, 1, 0, occ) | get_ray_attacks(sq, -1, 0, occ) |
        get_ray_attacks(sq, 0, 1, occ) | get_ray_attacks(sq, 0, -1, occ);
    int r_piece = (attacker_color == WHITE) ? W_ROOK : B_ROOK;
    if (r_attacks &
        (board.piece_bitboards[r_piece] | board.piece_bitboards[q_piece]))
        return true;

    return false;
}

void generate_pseudo_legal_moves(const Board &board,
                                 std::vector<Move> &move_list)
{
    int us = board.side_to_move;
    int them = ~us & 1;
    uint64_t own_occ = board.color_bitboards[us];
    uint64_t opp_occ = board.color_bitboards[them];
    uint64_t all_occ = board.color_bitboards[BOTH];

    int p_start = (us == WHITE) ? W_PAWN : B_PAWN;
    int p_end = (us == WHITE) ? W_KING : B_KING;

    for (int piece = p_start; piece <= p_end; piece++)
    {
        uint64_t bb = board.piece_bitboards[piece];
        while (bb)
        {
            int sq = std::countr_zero(bb);
            bb &= bb - 1;

            if (piece == W_PAWN || piece == B_PAWN)
            {
                int fwd = (us == WHITE) ? 8 : -8;
                int target = sq + fwd;
                bool is_promo = (us == WHITE && target / 8 == 7) ||
                                (us == BLACK && target / 8 == 0);

                // 1. Single & Double Pushes
                if (target >= 0 && target < 64 && !(all_occ & (1ULL << target)))
                {
                    if (is_promo)
                    {
                        for (int prom : {(us == WHITE ? W_QUEEN : B_QUEEN),
                                         (us == WHITE ? W_ROOK : B_ROOK),
                                         (us == WHITE ? W_BISHOP : B_BISHOP),
                                         (us == WHITE ? W_KNIGHT : B_KNIGHT)})
                            move_list.push_back({(uint8_t)sq, (uint8_t)target,
                                                 (uint8_t)prom, 0, 0});
                    }
                    else
                    {
                        move_list.push_back(
                            {(uint8_t)sq, (uint8_t)target, EMPTY, 0, 0});
                        int double_push = sq + 2 * fwd;
                        if (((us == WHITE && sq / 8 == 1) ||
                             (us == BLACK && sq / 8 == 6)) &&
                            !(all_occ & (1ULL << double_push)))
                            move_list.push_back({(uint8_t)sq,
                                                 (uint8_t)double_push, EMPTY, 0,
                                                 0});
                    }
                }

                // 2. Pawn Captures
                for (int df : {-1, 1})
                {
                    int c_file = (sq % 8) + df;
                    if (c_file >= 0 && c_file < 8)
                    {
                        int c_target = sq + fwd + df;
                        if (c_target >= 0 && c_target < 64)
                        {
                            if (opp_occ & (1ULL << c_target))
                            {
                                if (is_promo)
                                { // Error 3 Fixed: Capture + Promotion
                                    for (int prom :
                                         {(us == WHITE ? W_QUEEN : B_QUEEN),
                                          (us == WHITE ? W_ROOK : B_ROOK),
                                          (us == WHITE ? W_BISHOP : B_BISHOP),
                                          (us == WHITE ? W_KNIGHT : B_KNIGHT)})
                                        move_list.push_back(
                                            {(uint8_t)sq, (uint8_t)c_target,
                                             (uint8_t)prom, 1, 0});
                                }
                                else
                                {
                                    move_list.push_back({(uint8_t)sq,
                                                         (uint8_t)c_target,
                                                         EMPTY, 1, 0});
                                }
                            }
                            else if (c_target == board.en_passant_sq)
                            {
                                move_list.push_back({(uint8_t)sq,
                                                     (uint8_t)c_target, EMPTY,
                                                     2, 0});
                            }
                        }
                    }
                }
            }
            // 3. Knight and King Standard Moves
            else if (piece == W_KNIGHT || piece == B_KNIGHT ||
                     piece == W_KING || piece == B_KING)
            {
                uint64_t attacks = (piece == W_KNIGHT || piece == B_KNIGHT)
                                       ? KnightAttacks[sq]
                                       : KingAttacks[sq];
                attacks &= ~own_occ;
                while (attacks)
                {
                    int t = std::countr_zero(attacks);
                    attacks &= attacks - 1;
                    uint8_t flag = (opp_occ & (1ULL << t)) ? 1 : 0;
                    move_list.push_back(
                        {(uint8_t)sq, (uint8_t)t, EMPTY, flag, 0});
                }

                // Error 1 Fixed: Castling Logic
                if (piece == W_KING)
                {
                    if ((board.castling_rights & WK) &&
                        !(all_occ & ((1ULL << F1) | (1ULL << G1))) &&
                        !is_square_attacked(board, E1, BLACK) &&
                        !is_square_attacked(board, F1, BLACK))
                        move_list.push_back(
                            {(uint8_t)E1, (uint8_t)G1, EMPTY, 3, 0});
                    if ((board.castling_rights & WQ) &&
                        !(all_occ &
                          ((1ULL << B1) | (1ULL << C1) | (1ULL << D1))) &&
                        !is_square_attacked(board, E1, BLACK) &&
                        !is_square_attacked(board, D1, BLACK))
                        move_list.push_back(
                            {(uint8_t)E1, (uint8_t)C1, EMPTY, 3, 0});
                }
                else if (piece == B_KING)
                {
                    if ((board.castling_rights & BK) &&
                        !(all_occ & ((1ULL << F8) | (1ULL << G8))) &&
                        !is_square_attacked(board, E8, WHITE) &&
                        !is_square_attacked(board, F8, WHITE))
                        move_list.push_back(
                            {(uint8_t)E8, (uint8_t)G8, EMPTY, 3, 0});
                    if ((board.castling_rights & BQ) &&
                        !(all_occ &
                          ((1ULL << B8) | (1ULL << C8) | (1ULL << D8))) &&
                        !is_square_attacked(board, E8, WHITE) &&
                        !is_square_attacked(board, D8, WHITE))
                        move_list.push_back(
                            {(uint8_t)E8, (uint8_t)C8, EMPTY, 3, 0});
                }
            }
            // 4. Sliding Pieces (Rooks, Bishops, Queens)
            else
            {
                uint64_t attacks = 0;
                if (piece == W_BISHOP || piece == B_BISHOP ||
                    piece == W_QUEEN || piece == B_QUEEN)
                    attacks |= get_ray_attacks(sq, 1, 1, all_occ) |
                               get_ray_attacks(sq, 1, -1, all_occ) |
                               get_ray_attacks(sq, -1, 1, all_occ) |
                               get_ray_attacks(sq, -1, -1, all_occ);
                if (piece == W_ROOK || piece == B_ROOK || piece == W_QUEEN ||
                    piece == B_QUEEN)
                    attacks |= get_ray_attacks(sq, 1, 0, all_occ) |
                               get_ray_attacks(sq, -1, 0, all_occ) |
                               get_ray_attacks(sq, 0, 1, all_occ) |
                               get_ray_attacks(sq, 0, -1, all_occ);

                attacks &= ~own_occ;
                while (attacks)
                {
                    int t = std::countr_zero(attacks);
                    attacks &= attacks - 1;
                    uint8_t flag = (opp_occ & (1ULL << t)) ? 1 : 0;
                    move_list.push_back(
                        {(uint8_t)sq, (uint8_t)t, EMPTY, flag, 0});
                }
            }
        }
    }
}

uint64_t perft(Board &board, int depth)
{
    if (depth == 0)
        return 1ULL;
    uint64_t nodes = 0;
    std::vector<Move> move_list;
    generate_pseudo_legal_moves(board, move_list);

    for (const auto &move : move_list)
    {
        if (!board.make_move(move))
            continue;
        nodes += perft(board, depth - 1);
        board.unmake_move(move);
    }
    return nodes;
}