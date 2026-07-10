// Move generation implementasyonu.

#include "engine/movegen.hpp"

#include "engine/attacks.hpp"
#include "engine/bitboard.hpp"

namespace engine {

bool is_square_attacked(const Board& b, Square sq, Color by, Bitboard occ) {
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

bool is_square_attacked(const Board& b, Square sq, Color by) {
    return is_square_attacked(b, sq, by, b.occupancy());
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

namespace {

// Bir düğümde BİR KEZ hesaplanan legallik bağlamı: şahı çeken taşlar, tek çekte
// izin verilen hedef kareler ve kendi şahımızı bir rakip ışınına karşı kapatan
// (pinli) taşlarımız. Bu üçlüyle her pseudo-legal hamlenin legalliği tahtayı
// KOPYALAMADAN, sabit sayıda bit işlemiyle karara bağlanır.
struct LegalityContext {
    Square   ksq;
    Bitboard checkers;    // şahımıza saldıran rakip taşlar
    Bitboard check_mask;  // tek çekte hedef kare bu kümede olmalı (araya gir ya da al)
    Bitboard pinned;      // şahımızla bir rakip sniper arasındaki TEK taş olan kendi taşlarımız
    int      num_checkers;
};

// `sq` karesine saldıran `by` renginden TÜM taşlar (küme biçimi).
Bitboard attackers_of(const Board& b, Square sq, Color by, Bitboard occ) {
    const Bitboard diag = (b.pieces[BISHOP] | b.pieces[QUEEN]) & b.colors[by];
    const Bitboard orth = (b.pieces[ROOK] | b.pieces[QUEEN]) & b.colors[by];

    return (pawn_attacks(~by, sq) & b.pieces[PAWN] & b.colors[by])
         | (knight_attacks(sq) & b.pieces[KNIGHT] & b.colors[by])
         | (king_attacks(sq) & b.pieces[KING] & b.colors[by])
         | (bishop_attacks(sq, occ) & diag)
         | (rook_attacks(sq, occ) & orth);
}

LegalityContext make_context(const Board& b, Color us) {
    const Color    them = ~us;
    const Bitboard occ  = b.occupancy();

    LegalityContext ctx{};
    ctx.ksq          = b.king_square(us);
    ctx.checkers     = attackers_of(b, ctx.ksq, them, occ);
    ctx.num_checkers = popcount(ctx.checkers);

    // Tek çekte: çeken taşı al ya da (sliding ise) arasına gir. Çift çekte maske
    // kullanılmaz (yalnız şah hamlesi legaldir). Çek yoksa maske serbest.
    if (ctx.num_checkers == 1) {
        Square checker = lsb(ctx.checkers);
        ctx.check_mask = between_bb(ctx.ksq, checker) | ctx.checkers;
    } else {
        ctx.check_mask = ~Bitboard{0};
    }

    // Pin tespiti (sniper algoritması): şahtan BOŞ tahta ışınları çekip aynı ışın
    // üzerindeki rakip slider'ları bul; arada tam bir taş varsa o taş pinlidir.
    // Işınlar boş tahtada üretildiği için araya giren taşlar körleştirmez.
    const Bitboard snipers =
        ((rook_attacks(ctx.ksq, 0) & (b.pieces[ROOK] | b.pieces[QUEEN])) |
         (bishop_attacks(ctx.ksq, 0) & (b.pieces[BISHOP] | b.pieces[QUEEN]))) &
        b.colors[them];

    Bitboard s = snipers;
    while (s) {
        Square   sniper  = pop_lsb(s);
        Bitboard blocker = between_bb(ctx.ksq, sniper) & occ;
        // Tam bir taş (b & b-1 == 0 -> en fazla bir bit) ve o taş bizimse pinli.
        if (blocker && (blocker & (blocker - 1)) == 0)
            ctx.pinned |= blocker & b.colors[us];
    }

    return ctx;
}

// En passant legalliği: tek hamlede BİR SIRADAN İKİ taş kalkar (oynayan piyon ve
// alınan piyon), bu yüzden pin makinesine görünmez — ayrı, tam bir occupancy
// testi şarttır. Klasik tuzak: yan yana duran şah + kale/vezir hattında ep
// oynamak şahı açar.
bool en_passant_is_legal(const Board& b, Move m, Color us, Square ksq) {
    const Color  them = ~us;
    const Square from = m.from();
    const Square to   = m.to();
    // Alınan piyon hedef karenin ARKASINDA durur (ep hedefi boş karedir).
    const Square captured =
        static_cast<Square>(static_cast<int>(to) + (us == WHITE ? -8 : 8));

    const Bitboard occ = (b.occupancy() ^ square_bb(from) ^ square_bb(captured)) |
                         square_bb(to);
    // Alınan piyon artık tahtada değil (çek veren piyon o olabilir).
    const Bitboard their_pawns = (b.pieces[PAWN] & b.colors[them]) ^ square_bb(captured);

    if (pawn_attacks(us, ksq) & their_pawns)
        return false;
    if (knight_attacks(ksq) & b.pieces[KNIGHT] & b.colors[them])
        return false;
    if (king_attacks(ksq) & b.pieces[KING] & b.colors[them])
        return false;
    if (bishop_attacks(ksq, occ) & (b.pieces[BISHOP] | b.pieces[QUEEN]) & b.colors[them])
        return false;
    if (rook_attacks(ksq, occ) & (b.pieces[ROOK] | b.pieces[QUEEN]) & b.colors[them])
        return false;

    return true;
}

// Bir pseudo-legal hamlenin legalliği — tahta kopyalamadan.
bool is_legal(const Board& b, Move m, Color us, const LegalityContext& ctx) {
    const Square from = m.from();
    const Square to   = m.to();

    if (m.type() == EN_PASSANT)
        return en_passant_is_legal(b, m, us, ctx.ksq);

    if (from == ctx.ksq) {
        // Rok zaten generate_castling'de tam legal üretiliyor (çek, geçilen ve
        // varış kareleri orada sınanır).
        if (m.type() == CASTLING)
            return true;
        // Şah kendi eski karesinden ÇIKARILIR: aksi halde çek veren ışında geriye
        // kaçış, şahın kendi gölgesi yüzünden güvenli görünür.
        return !is_square_attacked(b, to, ~us, b.occupancy() ^ square_bb(ctx.ksq));
    }

    // Buradan sonrası şah dışı taşlar.
    if (ctx.num_checkers >= 2)
        return false;  // çift çekte yalnız şah hamlesi kurtarır
    if (!test_bit(ctx.check_mask, to))
        return false;  // tek çekte: ya çekeni al ya araya gir (çek yoksa maske serbest)
    if (test_bit(ctx.pinned, from) && !test_bit(line_bb(ctx.ksq, from), to))
        return false;  // pinli taş yalnız şah-pinner ışını üzerinde oynayabilir

    return true;
}

}  // namespace

void generate_legal(const Board& b, MoveList& list) {
    const Color us = b.side_to_move;

    MoveList pseudo;
    generate_pseudo(b, pseudo);

    const LegalityContext ctx = make_context(b, us);

    for (Move m : pseudo) {
        if (is_legal(b, m, us, ctx))
            list.add(m);
    }
}

void generate_legal_reference(const Board& b, MoveList& list) {
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
