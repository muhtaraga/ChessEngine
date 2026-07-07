// Move generation implementasyonu.

#include "engine/movegen.hpp"

#include "engine/attacks.hpp"
#include "engine/bitboard.hpp"

namespace engine {

bool is_square_attacked(const Board& b, Square sq, Color by) {
    Bitboard occ = b.occupancy();

    // Piyon: sq karesini 'by' piyonu vuruyorsa, o piyon pawn_attacks(~by, sq)
    // kümesindedir (piyon saldırıları renk simetriktir).
    if (pawn_attacks(~by, sq) & b.pieces[PAWN] & b.colors[by])
        return true;
    if (knight_attacks(sq) & b.pieces[KNIGHT] & b.colors[by])
        return true;
    if (king_attacks(sq) & b.pieces[KING] & b.colors[by])
        return true;

    Bitboard diag = (b.pieces[BISHOP] | b.pieces[QUEEN]) & b.colors[by];
    if (bishop_attacks(sq, occ) & diag)
        return true;

    Bitboard orth = (b.pieces[ROOK] | b.pieces[QUEEN]) & b.colors[by];
    if (rook_attacks(sq, occ) & orth)
        return true;

    return false;
}

namespace {

// Bir piyon hedefi promosyon sırasındaysa 4 promosyon hamlesi, değilse tek
// normal/uygun hamle ekler.
void add_pawn_move(MoveList& list, Square from, Square to, int promo_rank,
                   MoveType base_type) {
    if (rank_of(to) == promo_rank) {
        list.add(Move::make(from, to, PROMOTION, QUEEN));
        list.add(Move::make(from, to, PROMOTION, ROOK));
        list.add(Move::make(from, to, PROMOTION, BISHOP));
        list.add(Move::make(from, to, PROMOTION, KNIGHT));
    } else {
        list.add(Move::make(from, to, base_type));
    }
}

void generate_pawn_moves(const Board& b, MoveList& list, Color us) {
    const Color    them   = ~us;
    const Bitboard occ    = b.occupancy();
    const Bitboard empty  = ~occ;
    const Bitboard enemy  = b.colors[them];
    Bitboard pawns        = b.pieces[PAWN] & b.colors[us];

    const int push       = (us == WHITE) ? 8 : -8;
    const int start_rank = (us == WHITE) ? 1 : 6;
    const int promo_rank = (us == WHITE) ? 7 : 0;

    while (pawns) {
        Square from = pop_lsb(pawns);

        // Tek ileri itme
        Square to1 = static_cast<Square>(static_cast<int>(from) + push);
        if (test_bit(empty, to1)) {
            add_pawn_move(list, from, to1, promo_rank, NORMAL);

            // Çift ileri itme (yalnızca başlangıç sırasından, iki kare boşsa)
            if (rank_of(from) == start_rank) {
                Square to2 = static_cast<Square>(static_cast<int>(from) + 2 * push);
                if (test_bit(empty, to2))
                    list.add(Move::make(from, to2, NORMAL));
            }
        }

        // Vuruşlar (promosyon dahil)
        Bitboard caps = pawn_attacks(us, from) & enemy;
        while (caps) {
            Square to = pop_lsb(caps);
            add_pawn_move(list, from, to, promo_rank, NORMAL);
        }

        // En passant
        if (b.en_passant != SQ_NONE &&
            test_bit(pawn_attacks(us, from), b.en_passant))
            list.add(Move::make(from, b.en_passant, EN_PASSANT));
    }
}

// step/slider taşlar için hedef bitboard'ından NORMAL hamleler ekler.
void add_moves_from(MoveList& list, Square from, Bitboard targets) {
    while (targets) {
        Square to = pop_lsb(targets);
        list.add(Move::make(from, to, NORMAL));
    }
}

void generate_castling(const Board& b, MoveList& list, Color us) {
    const Color    them = ~us;
    const Bitboard occ  = b.occupancy();

    // Şah çekteyse rok yapılamaz.
    Square ksq = b.king_square(us);
    if (is_square_attacked(b, ksq, them))
        return;

    if (us == WHITE) {
        if ((b.castling_rights & WHITE_OO) &&
            !test_bit(occ, F1) && !test_bit(occ, G1) &&
            !is_square_attacked(b, F1, them) && !is_square_attacked(b, G1, them))
            list.add(Move::make(E1, G1, CASTLING));

        if ((b.castling_rights & WHITE_OOO) &&
            !test_bit(occ, D1) && !test_bit(occ, C1) && !test_bit(occ, B1) &&
            !is_square_attacked(b, D1, them) && !is_square_attacked(b, C1, them))
            list.add(Move::make(E1, C1, CASTLING));
    } else {
        if ((b.castling_rights & BLACK_OO) &&
            !test_bit(occ, F8) && !test_bit(occ, G8) &&
            !is_square_attacked(b, F8, them) && !is_square_attacked(b, G8, them))
            list.add(Move::make(E8, G8, CASTLING));

        if ((b.castling_rights & BLACK_OOO) &&
            !test_bit(occ, D8) && !test_bit(occ, C8) && !test_bit(occ, B8) &&
            !is_square_attacked(b, D8, them) && !is_square_attacked(b, C8, them))
            list.add(Move::make(E8, C8, CASTLING));
    }
}

}  // namespace

void generate_pseudo(const Board& b, MoveList& list) {
    const Color    us     = b.side_to_move;
    const Bitboard own    = b.colors[us];
    const Bitboard occ    = b.occupancy();
    const Bitboard target = ~own;  // kendi taşları hariç her yer

    generate_pawn_moves(b, list, us);

    // At
    Bitboard knights = b.pieces[KNIGHT] & own;
    while (knights) {
        Square from = pop_lsb(knights);
        add_moves_from(list, from, knight_attacks(from) & target);
    }

    // Fil
    Bitboard bishops = b.pieces[BISHOP] & own;
    while (bishops) {
        Square from = pop_lsb(bishops);
        add_moves_from(list, from, bishop_attacks(from, occ) & target);
    }

    // Kale
    Bitboard rooks = b.pieces[ROOK] & own;
    while (rooks) {
        Square from = pop_lsb(rooks);
        add_moves_from(list, from, rook_attacks(from, occ) & target);
    }

    // Vezir
    Bitboard queens = b.pieces[QUEEN] & own;
    while (queens) {
        Square from = pop_lsb(queens);
        add_moves_from(list, from, queen_attacks(from, occ) & target);
    }

    // Şah (roksuz)
    Square ksq = b.king_square(us);
    add_moves_from(list, ksq, king_attacks(ksq) & target);

    // Rok (yalnızca tam legal olduğunda)
    generate_castling(b, list, us);
}

void generate_legal(const Board& b, MoveList& list) {
    const Color us = b.side_to_move;

    MoveList pseudo;
    generate_pseudo(b, pseudo);

    for (Move m : pseudo) {
        Board next = b;
        next.do_move(m);
        // Hamleden sonra bizim şahımız karşı renk tarafından tehdit ediliyorsa
        // hamle illegaldir.
        if (!is_square_attacked(next, next.king_square(us), ~us))
            list.add(m);
    }
}

}  // namespace engine
