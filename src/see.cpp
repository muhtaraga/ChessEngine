// SEE (Static Exchange Evaluation) — swap algoritması implementasyonu.

#include "engine/see.hpp"

#include <algorithm>

#include "engine/attacks.hpp"
#include "engine/bitboard.hpp"
#include "engine/eval.hpp"

namespace engine {

namespace {

// SEE'de kullanılan taş değeri. Materyal tablosuyla aynı; yalnız ŞAH için büyük
// bir sabit — böylece şah "en ucuz saldıran" sıralamasında en sona düşer ve
// savunmalı bir kareye şahla girip onu kaybetmek (illegal) asla tercih edilmez.
constexpr int see_value(PieceType pt) {
    return (pt == KING) ? 10000 : MaterialValue[pt];
}

// `to` karesine saldıran TÜM taşlar (her iki renk), verilen `occ` doluluğuyla.
// is_square_attacked (movegen.cpp) deseninin tam kümesini döndüren biçimi:
// piyon saldırıları renk-simetrik olduğundan bir rengin piyonu `to`'yu vuruyorsa
// o piyon, karşı renk piyonunun `to`'dan saldırı kümesindedir.
Bitboard attackers_to(const Board& b, Square to, Bitboard occ) {
    const Bitboard pawns   = b.pieces[PAWN];
    const Bitboard knights = b.pieces[KNIGHT];
    const Bitboard kings   = b.pieces[KING];
    const Bitboard bishopsQueens = b.pieces[BISHOP] | b.pieces[QUEEN];
    const Bitboard rooksQueens   = b.pieces[ROOK]   | b.pieces[QUEEN];

    return (pawn_attacks(BLACK, to) & pawns & b.colors[WHITE])
         | (pawn_attacks(WHITE, to) & pawns & b.colors[BLACK])
         | (knight_attacks(to) & knights)
         | (king_attacks(to)   & kings)
         | (bishop_attacks(to, occ) & bishopsQueens)
         | (rook_attacks(to, occ)   & rooksQueens);
}

}  // namespace

int see(const Board& b, Move m) {
    const Square   to   = m.to();
    const Square   from = m.from();
    const MoveType mt   = m.type();

    Bitboard occ = b.occupancy();

    // İlk yakalanan taşın değeri (gain[0]).
    PieceType captured;
    if (mt == EN_PASSANT) {
        captured = PAWN;
        // Ep ile alınan piyon, hedefin arkasında (from ile aynı sırada) durur;
        // tahtadan kalkar. X-ray (arkadaki kale/vezir) için occ'tan çıkarılmalı.
        Square capsq = make_square(file_of(to), rank_of(from));
        occ ^= square_bb(capsq);
    } else {
        captured = b.type_on(to);  // ön koşul: m bir yakalama
    }

    int gain[32];
    int d = 0;
    gain[0] = see_value(captured);

    PieceType aPiece  = b.type_on(from);  // ilk saldıran (bu turda vuran)
    Bitboard  from_bb = square_bb(from);
    Color     side    = ~b.side_to_move;  // sırayla karşı taraf yeniden alır

    do {
        ++d;
        // Bu taraf vurursa spekülatif kazanç: kendi taşının değeri eksi önceki
        // (rakibin bu kareye yatırdığı) kazanç.
        gain[d] = see_value(aPiece) - gain[d - 1];

        // Güvenli budama: bu noktadan sonra sonucu değiştiremeyecek dal kesilir
        // (max, minimax geri-katlamada kullanılacak sınırı verir).
        if (std::max(-gain[d - 1], gain[d]) < 0)
            break;

        // Kullanılan saldıranı tahtadan çıkar; saldırı kümesini occ'a göre yeniden
        // hesapla (x-ray sliderlar açılır, kaldırılan taşlar occ maskesiyle düşer).
        occ ^= from_bb;
        Bitboard attadef = attackers_to(b, to, occ) & occ;

        // `side`in en ucuz saldıranını seç (PAWN..KING sırası = ucuzdan pahalıya).
        from_bb = 0;
        Bitboard side_att = attadef & b.colors[side];
        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard s = side_att & b.pieces[pt];
            if (s) {
                from_bb = square_bb(lsb(s));
                aPiece  = static_cast<PieceType>(pt);
                break;
            }
        }
        side = ~side;
    } while (from_bb);

    // Minimax geri-katlama: her taraf, devam etmek zararlıysa alışverişi durdurur.
    while (--d)
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);

    return gain[0];
}

}  // namespace engine
