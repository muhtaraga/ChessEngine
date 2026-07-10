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

// step/slider taşlar için hedef bitboard'ından NORMAL hamleler ekler.
void add_moves_from(MoveList& list, Square from, Bitboard targets) {
    while (targets) {
        Square to = pop_lsb(targets);
        list.add(Move::make(from, to, NORMAL));
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

// Rok üretimi, "şah çekte değil" ön koşulu ÇAĞIRANDA sağlanmış olarak. Legal
// üretici bunu ctx.checkers'tan bedavaya bilir; pseudo üretici aşağıdaki
// sarmalayıcıyla test eder.
void generate_castling_unchecked(const Board& b, MoveList& list, Color us) {
    const Color    them = ~us;
    const Bitboard occ  = b.occupancy();

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

void generate_castling(const Board& b, MoveList& list, Color us) {
    // Şah çekteyse rok yapılamaz.
    if (is_square_attacked(b, b.king_square(us), ~us))
        return;
    generate_castling_unchecked(b, list, us);
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

// En passant legalliği: tek hamlede BİR SIRADAN İKİ taş kalkar (oynayan piyon ve
// alınan piyon), bu yüzden pin makinesine ve check_mask'e görünmez — ayrı, tam bir
// occupancy testi şarttır. Klasik tuzak: yan yana duran şah + kale/vezir hattında
// ep oynamak şahı açar. Bu test tam olduğundan ep, üretimde hiçbir maskeye tabi
// tutulmaz (çift çekte bile denenir: alınan piyon çekenlerden biri olabilir).
bool en_passant_is_legal(const Board& b, Square from, Square to, Color us, Square ksq) {
    const Color them = ~us;
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

// Bir taş türünün `sq`'den saldırdığı kareler. Yalnız promosyon sonrası taş için
// gerekli (piyon ve şah çağrılmaz).
Bitboard piece_attacks(PieceType pt, Square sq, Bitboard occ) {
    switch (pt) {
        case KNIGHT: return knight_attacks(sq);
        case BISHOP: return bishop_attacks(sq, occ);
        case ROOK:   return rook_attacks(sq, occ);
        case QUEEN:  return queen_attacks(sq, occ);
        default:     return 0;
    }
}

// Pinli bir taş yalnız şah-pinner ışını üzerinde oynayabilir; pinli değilse serbest.
Bitboard pin_mask(const MoveGenContext& ctx, Square from) {
    return test_bit(ctx.pinned, from) ? line_bb(ctx.ksq, from) : ~Bitboard{0};
}

// Piyon üretimi, doğrudan legal. Tarama düzeni generate_pawn_moves ile BİREBİR
// aynı (tek itme -> çift itme -> vuruşlar -> ep); yalnızca hedefler ctx maskeleriyle
// daraltılır. Noisy=true iken sessiz hamleler (promosyona varmayan itmeler) atlanır.
template <bool Noisy>
void generate_pawns(const Board& b, MoveList& list, Color us, const MoveGenContext& ctx) {
    const Bitboard empty = ~b.occupancy();
    const Bitboard enemy = b.colors[~us];
    Bitboard pawns       = b.pieces[PAWN] & b.colors[us];

    const int push       = (us == WHITE) ? 8 : -8;
    const int start_rank = (us == WHITE) ? 1 : 6;
    const int promo_rank = (us == WHITE) ? 7 : 0;

    while (pawns) {
        Square from = pop_lsb(pawns);
        const Bitboard mask = ctx.check_mask & pin_mask(ctx, from);

        // Tek ileri itme. Boşluk testi maskeden BAĞIMSIZ: to1 maskeyi geçmese bile
        // boşsa çift itme denenebilir (araya girme çoğu zaman iki kare ileridedir).
        Square to1 = static_cast<Square>(static_cast<int>(from) + push);
        if (test_bit(empty, to1)) {
            const bool promo = rank_of(to1) == promo_rank;
            if ((!Noisy || promo) && test_bit(mask, to1))
                add_pawn_move(list, from, to1, promo_rank, NORMAL);

            if (!Noisy && rank_of(from) == start_rank) {
                Square to2 = static_cast<Square>(static_cast<int>(from) + 2 * push);
                if (test_bit(empty, to2) && test_bit(mask, to2))
                    list.add(Move::make(from, to2, NORMAL));
            }
        }

        // Vuruşlar (promosyon dahil). check_mask çeken taşı içerdiğinden "çekeni al"
        // kendiliğinden çalışır.
        Bitboard caps = pawn_attacks(us, from) & enemy & mask;
        while (caps) {
            Square to = pop_lsb(caps);
            add_pawn_move(list, from, to, promo_rank, NORMAL);
        }

        // En passant: maskesiz, tam occupancy testiyle (yukarıdaki nota bak).
        if (b.en_passant != SQ_NONE &&
            test_bit(pawn_attacks(us, from), b.en_passant) &&
            en_passant_is_legal(b, from, b.en_passant, us, ctx.ksq))
            list.add(Move::make(from, b.en_passant, EN_PASSANT));
    }
}

// Doğrudan legal üretim. generate_pseudo'nun tarama düzeni birebir korunur
// (piyon -> at -> fil -> kale -> vezir -> şah -> rok, her taşta pop_lsb LSB->MSB),
// böylece hamle sırası — dolayısıyla move ordering ve arama ağacı — değişmez.
template <bool Noisy>
void generate_all(const Board& b, MoveList& list, const MoveGenContext& ctx) {
    const Color    us   = b.side_to_move;
    const Bitboard own  = b.colors[us];
    const Bitboard occ  = b.occupancy();

    // Şah dışı taşların taban hedefi. Çift çekte check_mask == 0 -> hiçbiri üretilmez
    // (ep hariç: o kendi tam testine tabi).
    const Bitboard base   = Noisy ? b.colors[~us] : ~own;
    const Bitboard target = ctx.check_mask & base;

    generate_pawns<Noisy>(b, list, us, ctx);

    // At: pinli at asla oynayamaz (L hamlesi şah-pinner ışını üzerinde kalamaz).
    Bitboard knights = b.pieces[KNIGHT] & own & ~ctx.pinned;
    while (knights) {
        Square from = pop_lsb(knights);
        add_moves_from(list, from, knight_attacks(from) & target);
    }

    Bitboard bishops = b.pieces[BISHOP] & own;
    while (bishops) {
        Square from = pop_lsb(bishops);
        add_moves_from(list, from, bishop_attacks(from, occ) & target & pin_mask(ctx, from));
    }

    Bitboard rooks = b.pieces[ROOK] & own;
    while (rooks) {
        Square from = pop_lsb(rooks);
        add_moves_from(list, from, rook_attacks(from, occ) & target & pin_mask(ctx, from));
    }

    Bitboard queens = b.pieces[QUEEN] & own;
    while (queens) {
        Square from = pop_lsb(queens);
        add_moves_from(list, from, queen_attacks(from, occ) & target & pin_mask(ctx, from));
    }

    // Şah: check_mask UYGULANMAZ (kaçış maskeye tabi değil). Şah kendi eski
    // karesinden ÇIKARILIR: aksi halde çek veren ışında geriye kaçış, şahın kendi
    // gölgesi yüzünden güvenli görünür.
    const Bitboard king_occ = occ ^ square_bb(ctx.ksq);
    Bitboard king_targets   = king_attacks(ctx.ksq) & base;
    while (king_targets) {
        Square to = pop_lsb(king_targets);
        if (!is_square_attacked(b, to, ~us, king_occ))
            list.add(Move::make(ctx.ksq, to, NORMAL));
    }

    // Rok asla yakalama değildir -> gürültülü üretimde yok. Çekteyken de yasak.
    if constexpr (!Noisy) {
        if (ctx.checkers == 0)
            generate_castling_unchecked(b, list, us);
    }
}

}  // namespace

MoveGenContext make_context(const Board& b) {
    const Color    us   = b.side_to_move;
    const Color    them = ~us;
    const Bitboard occ  = b.occupancy();

    MoveGenContext ctx{};
    ctx.ksq          = b.king_square(us);
    ctx.checkers     = attackers_of(b, ctx.ksq, them, occ);
    ctx.num_checkers = popcount(ctx.checkers);

    // Çek yoksa maske serbest. Tek çekte: çekeni al ya da (sliding ise) arasına gir.
    // Çift çekte maske BOŞ -> şah dışı hiçbir taş oynayamaz (yalnız şah kurtarır).
    if (ctx.num_checkers == 0) {
        ctx.check_mask = ~Bitboard{0};
    } else if (ctx.num_checkers == 1) {
        Square checker = lsb(ctx.checkers);
        ctx.check_mask = between_bb(ctx.ksq, checker) | ctx.checkers;
    } else {
        ctx.check_mask = 0;
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

CheckInfo make_check_info(const Board& b) {
    const Color    us   = b.side_to_move;
    const Color    them = ~us;
    const Bitboard occ  = b.occupancy();

    CheckInfo ci{};
    ci.oksq = b.king_square(them);

    // Piyon: bizim piyonumuz `sq`'den oksq'ye çek verir <=> sq, pawn_attacks(them, oksq)
    // kümesindedir (is_square_attacked'deki aynı simetri).
    ci.check_squares[PAWN]   = pawn_attacks(them, ci.oksq);
    ci.check_squares[KNIGHT] = knight_attacks(ci.oksq);
    ci.check_squares[BISHOP] = bishop_attacks(ci.oksq, occ);
    ci.check_squares[ROOK]   = rook_attacks(ci.oksq, occ);
    ci.check_squares[QUEEN]  = ci.check_squares[BISHOP] | ci.check_squares[ROOK];
    ci.check_squares[KING]   = 0;  // şah çek veremez (rok'un kalesi ayrı ele alınır)

    // Keşif çeki adayları: make_context'teki sniper algoritmasının aynası — ışınlar
    // RAKİP şahtan çekilir, sniper'lar BİZİM slider'larımızdır. Araya giren taş sayısı
    // (her iki renk dahil) tam 1 ise ve o taş bizimse, kalkınca çek açar.
    const Bitboard snipers =
        ((rook_attacks(ci.oksq, 0) & (b.pieces[ROOK] | b.pieces[QUEEN])) |
         (bishop_attacks(ci.oksq, 0) & (b.pieces[BISHOP] | b.pieces[QUEEN]))) &
        b.colors[us];

    Bitboard s = snipers;
    while (s) {
        Square   sniper  = pop_lsb(s);
        Bitboard blocker = between_bb(ci.oksq, sniper) & occ;
        if (blocker && (blocker & (blocker - 1)) == 0)
            ci.blockers |= blocker & b.colors[us];
    }

    return ci;
}

bool gives_check(const Board& b, Move m, const CheckInfo& ci) {
    const Color  us   = b.side_to_move;
    const Square from = m.from();
    const Square to   = m.to();

    // Rok: şah ve kale AYNI anda iki kare boşaltıp iki kare doldurur; hem doğrudan
    // (kale) hem keşif çeki yolları bit hilesiyle tuzaklı. Düğüm başına en fazla iki
    // rok hamlesi var -> doğruluk uğruna kopya-tabanlı referansa düşülür.
    if (m.type() == CASTLING) {
        Board next = b;
        next.do_move(m);
        return is_square_attacked(next, ci.oksq, us);
    }

    // Doğrudan çek: taş hedefe oynayıp oksq'yi vuruyor mu? (Promosyonda bu test
    // PİYON tablosuna bakar ve daima false verir — promosyon karesi 8. sırada, piyon
    // çek kareleri oksq'nin bir sırasında; aşağıdaki switch gerçek taşla karar verir.)
    if (test_bit(ci.check_squares[b.type_on(from)], to))
        return true;

    // Keşif çeki: taşımız oksq-slider ışınından çıkıyorsa çek açılır. Işın üzerinde
    // kalıyorsa (line_bb hedefi içeriyor) hâlâ kapatıyor demektir.
    if (test_bit(ci.blockers, from) && !test_bit(line_bb(ci.oksq, from), to))
        return true;

    switch (m.type()) {
        case PROMOTION: {
            // Piyon `from`'u boşaltır; yeni taş `to`'dan saldırır.
            const Bitboard occ = b.occupancy() ^ square_bb(from);
            return test_bit(piece_attacks(m.promotion_type(), to, occ), ci.oksq);
        }
        case EN_PASSANT: {
            // İki kare aynı anda boşalır (from + alınan piyon) -> keşif testi yetmez,
            // slider'lar yeni occupancy ile yeniden sorgulanır. Atlar/piyonlar/şah
            // occupancy'ye bağlı olmadığından yukarıdaki doğrudan test onları kapsar.
            const Square capsq =
                static_cast<Square>(static_cast<int>(to) + (us == WHITE ? -8 : 8));
            const Bitboard occ =
                (b.occupancy() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
            const Bitboard our_bq = (b.pieces[BISHOP] | b.pieces[QUEEN]) & b.colors[us];
            const Bitboard our_rq = (b.pieces[ROOK]   | b.pieces[QUEEN]) & b.colors[us];
            return (bishop_attacks(ci.oksq, occ) & our_bq) ||
                   (rook_attacks(ci.oksq, occ) & our_rq);
        }
        default:
            return false;
    }
}

void generate_legal(const Board& b, MoveList& list, const MoveGenContext& ctx) {
    generate_all<false>(b, list, ctx);
}

void generate_legal(const Board& b, MoveList& list) {
    generate_all<false>(b, list, make_context(b));
}

void generate_noisy(const Board& b, MoveList& list, const MoveGenContext& ctx) {
    generate_all<true>(b, list, ctx);
}

void generate_noisy(const Board& b, MoveList& list) {
    generate_all<true>(b, list, make_context(b));
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
