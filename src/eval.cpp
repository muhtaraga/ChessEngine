// Statik değerlendirme implementasyonu (tapered eval: MG/EG interpolasyonu).

#include "engine/eval.hpp"

#include "engine/attacks.hpp"
#include "engine/bitboard.hpp"
#include "engine/board.hpp"

namespace engine {

int game_phase(const Board& b) {
    int phase = 0;
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        phase += PhaseWeight[pt] * popcount(b.pieces[pt]);
    // Erken promosyonla (ör. iki vezir) faz teorik tavanı aşabilir; tavanla sınırla.
    return phase > MAX_PHASE ? MAX_PHASE : phase;
}

void pawn_structure(const Board& b, int& mg, int& eg) {
    mg = 0;
    eg = 0;

    const Bitboard wp = b.pieces[PAWN] & b.colors[WHITE];
    const Bitboard bp = b.pieces[PAWN] & b.colors[BLACK];

    // --- Çift piyon (doubled): sütun başına sayılır (piyon başına değil, çift
    // sayımı önlemek için). Bir sütunda k piyon varsa (k-1) fazlalık cezalanır.
    for (int f = 0; f < 8; ++f) {
        int wc = popcount(wp & FileMask[f]);
        if (wc > 1) {
            mg += (wc - 1) * DoubledPenaltyMg;
            eg += (wc - 1) * DoubledPenaltyEg;
        }
        int bc = popcount(bp & FileMask[f]);
        if (bc > 1) {  // siyahın cezası beyaz bakışına + olarak yansır
            mg -= (bc - 1) * DoubledPenaltyMg;
            eg -= (bc - 1) * DoubledPenaltyEg;
        }
    }

    // --- İzole + geçer piyon: piyon başına.
    Bitboard w = wp;
    while (w) {
        Square sq = pop_lsb(w);
        if ((wp & AdjacentFileMask[file_of(sq)]) == 0) {  // izole
            mg += IsolatedPenaltyMg;
            eg += IsolatedPenaltyEg;
        }
        if ((bp & PassedMask[WHITE][sq]) == 0) {  // geçer (önünde rakip piyon yok)
            int r = rank_of(sq);
            mg += PassedBonusMg[r];
            eg += PassedBonusEg[r];
        }
    }

    Bitboard bb = bp;
    while (bb) {
        Square sq = pop_lsb(bb);
        if ((bp & AdjacentFileMask[file_of(sq)]) == 0) {  // siyah izole -> beyaz +
            mg -= IsolatedPenaltyMg;
            eg -= IsolatedPenaltyEg;
        }
        if ((wp & PassedMask[BLACK][sq]) == 0) {  // siyah geçer -> beyaz −
            int r = 7 - rank_of(sq);  // siyah için sıra aynalanır
            mg -= PassedBonusMg[r];
            eg -= PassedBonusEg[r];
        }
    }
}

void mobility(const Board& b, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    const Bitboard occ = b.occupancy();

    // Her renk için at/fil/kale/vezir; ulaşılabilir (dost olmayan) kare sayısı ×
    // tür ağırlığı. Beyaz +, siyah − (beyaz bakışı). Sliding taşlar magic
    // tablolarla, at sabit tabloyla; hepsi statik olarak init edilmiş durumda.
    for (Color c : {WHITE, BLACK}) {
        const Bitboard own  = b.colors[c];
        const int      sign = (c == WHITE) ? 1 : -1;

        auto add = [&](PieceType pt, Bitboard attacks) {
            int m = popcount(attacks & ~own);  // dost taşla dolu kareler hariç
            mg += sign * m * MobilityMg[pt];
            eg += sign * m * MobilityEg[pt];
        };

        Bitboard knights = b.pieces[KNIGHT] & own;
        while (knights) { Square s = pop_lsb(knights); add(KNIGHT, knight_attacks(s)); }

        Bitboard bishops = b.pieces[BISHOP] & own;
        while (bishops) { Square s = pop_lsb(bishops); add(BISHOP, bishop_attacks(s, occ)); }

        Bitboard rooks = b.pieces[ROOK] & own;
        while (rooks) { Square s = pop_lsb(rooks); add(ROOK, rook_attacks(s, occ)); }

        Bitboard queens = b.pieces[QUEEN] & own;
        while (queens) { Square s = pop_lsb(queens); add(QUEEN, queen_attacks(s, occ)); }
    }
}

int evaluate(const Board& b) {
    // Orta oyun ve oyun sonu puanları ayrı biriktirilir (beyaz bakışıyla).
    int mg = 0;
    int eg = 0;

    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        int material = MaterialValue[pt];  // faz-bağımsız (MG = EG)

        // Beyaz taşlar: +materyal +PST
        Bitboard wbb = b.pieces[pt] & b.colors[WHITE];
        while (wbb) {
            Square sq = pop_lsb(wbb);
            mg += material + PstMg[pt][sq];
            eg += material + PstEg[pt][sq];
        }

        // Siyah taşlar: -materyal -PST (dikey ayna: sq ^ 56)
        Bitboard bbb = b.pieces[pt] & b.colors[BLACK];
        while (bbb) {
            Square sq = static_cast<Square>(static_cast<int>(pop_lsb(bbb)) ^ 56);
            mg -= material + PstMg[pt][sq];
            eg -= material + PstEg[pt][sq];
        }
    }

    // Pawn structure (izole/çift/geçer) katkısı beyaz − siyah olarak eklenir.
    int pmg = 0, peg = 0;
    pawn_structure(b, pmg, peg);
    mg += pmg;
    eg += peg;

    // Mobility (at/fil/kale/vezir) katkısı beyaz − siyah olarak eklenir.
    int mmg = 0, meg = 0;
    mobility(b, mmg, meg);
    mg += mmg;
    eg += meg;

    // Faza göre interpolasyon: tam kadroda (phase=MAX) tamamen mg, oyun sonunda
    // (phase=0) tamamen eg. Materyal her iki uçta eşit olduğundan interpolasyondan
    // etkilenmez; yalnızca faza duyarlı PST (şu an şah) + pawn structure fazla göre kayar.
    int phase = game_phase(b);
    int score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    // Hamle sırası olan tarafın bakış açısına çevir.
    return (b.side_to_move == WHITE) ? score : -score;
}

}  // namespace engine
