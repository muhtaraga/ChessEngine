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

void bishop_pair(const Board& b, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    // İki (veya daha fazla) fili olan tarafa bonus. Basit ≥2 sayımı standart;
    // zıt-kare kontrolü (nadir aynı-renk çift promosyon) bilinçli ertelendi.
    if (popcount(b.pieces[BISHOP] & b.colors[WHITE]) >= 2) { mg += BishopPairMg; eg += BishopPairEg; }
    if (popcount(b.pieces[BISHOP] & b.colors[BLACK]) >= 2) { mg -= BishopPairMg; eg -= BishopPairEg; }
}

void rook_on_file(const Board& b, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    const Bitboard pawns = b.pieces[PAWN];

    for (Color c : {WHITE, BLACK}) {
        const int      sign       = (c == WHITE) ? 1 : -1;
        const Bitboard own_pawns  = pawns & b.colors[c];
        const Bitboard enemy_pawns = pawns & b.colors[~c];

        Bitboard rooks = b.pieces[ROOK] & b.colors[c];
        while (rooks) {
            Square   s = pop_lsb(rooks);
            Bitboard f = FileMask[file_of(s)];
            if ((own_pawns & f) == 0) {  // dost piyon yok
                if ((enemy_pawns & f) == 0) {  // hiç piyon yok -> açık sütun
                    mg += sign * RookOpenMg;
                    eg += sign * RookOpenEg;
                } else {  // yalnız rakip piyon -> yarı-açık sütun
                    mg += sign * RookSemiMg;
                    eg += sign * RookSemiEg;
                }
            }
        }
    }
}

void king_safety(const Board& b, int& mg, int& eg) {
    mg = 0;
    eg = 0;  // king safety yalnız orta oyun terimi; taper ile EG'de solar.
    const Bitboard occ = b.occupancy();

    // Her rengin ŞAHI için tehlike (danger, pozitif santipiyon) hesaplanır; bu,
    // o rengin puanını düşürür. Beyaz bakışı: beyaz şah tehlikesi mg'yi düşürür,
    // siyah şah tehlikesi mg'yi yükseltir -> mg += -sign * danger.
    for (Color c : {WHITE, BLACK}) {
        const int      sign = (c == WHITE) ? 1 : -1;
        const Square   ksq  = b.king_square(c);
        const int      kf   = file_of(ksq);
        const int      kr   = rank_of(ksq);

        int danger = 0;

        // --- Piyon kalkanı: şahın önündeki iki sıra, kf-1..kf+1 sütunları ---
        const Bitboard own_pawns = b.pieces[PAWN] & b.colors[c];
        for (int f = kf - 1; f <= kf + 1; ++f) {
            if (f < 0 || f > 7) continue;  // tahta kenarı: geçersiz sütun sayılmaz
            Bitboard front = 0;
            for (int step = 1; step <= 2; ++step) {
                int r = (c == WHITE) ? kr + step : kr - step;
                if (r < 0 || r > 7) continue;
                front |= Bitboard{1} << (r * 8 + f);
            }
            if ((own_pawns & front) == 0)  // bu sütunda kalkan piyonu yok
                danger += ShieldMissingPenalty;
        }

        // --- Şah bölgesi (king ring) saldırıları: rakip taşların bölgede vurduğu
        // kare sayısı × tür ağırlığı -> attack units -> SafetyTable. ---
        const Bitboard zone      = king_attacks(ksq);  // şahı çevreleyen 8 kare
        const Bitboard enemy     = b.colors[~c];
        int            units     = 0;

        auto accumulate = [&](PieceType pt, Bitboard attacks) {
            units += KingAttackWeight[pt] * popcount(attacks & zone);
        };

        Bitboard knights = b.pieces[KNIGHT] & enemy;
        while (knights) { Square s = pop_lsb(knights); accumulate(KNIGHT, knight_attacks(s)); }

        Bitboard bishops = b.pieces[BISHOP] & enemy;
        while (bishops) { Square s = pop_lsb(bishops); accumulate(BISHOP, bishop_attacks(s, occ)); }

        Bitboard rooks = b.pieces[ROOK] & enemy;
        while (rooks) { Square s = pop_lsb(rooks); accumulate(ROOK, rook_attacks(s, occ)); }

        Bitboard queens = b.pieces[QUEEN] & enemy;
        while (queens) { Square s = pop_lsb(queens); accumulate(QUEEN, queen_attacks(s, occ)); }

        if (units > 99) units = 99;  // tablo indeksini kırp
        danger += SafetyTable[units];

        mg += -sign * danger;  // tehlike, o rengin skorunu düşürür
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

    // Bishop pair katkısı.
    int bmg = 0, beg = 0;
    bishop_pair(b, bmg, beg);
    mg += bmg;
    eg += beg;

    // Kale açık/yarı-açık sütun katkısı.
    int rmg = 0, reg = 0;
    rook_on_file(b, rmg, reg);
    mg += rmg;
    eg += reg;

    // King safety katkısı (piyon kalkanı + şah bölgesi saldırıları). Yalnız MG
    // (keg her zaman 0); orta oyunda etkili, oyun sonunda taper ile solar.
    int kmg = 0, keg = 0;
    king_safety(b, kmg, keg);
    mg += kmg;
    eg += keg;

    // Faza göre interpolasyon: tam kadroda (phase=MAX) tamamen mg, oyun sonunda
    // (phase=0) tamamen eg. Materyal her iki uçta eşit olduğundan interpolasyondan
    // etkilenmez; yalnızca faza duyarlı PST (şu an şah) + pawn structure fazla göre kayar.
    int phase = game_phase(b);
    int score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    // Hamle sırası olan tarafın bakış açısına çevir, sonra tempo bonusunu ekle
    // (hamle hakkı: daima sıradaki tarafa +, faz-bağımsız). Flip'ten SONRA eklenir
    // ki tempo da işaret değiştirmesin (flip'ten önce eklersek siyahta - olurdu).
    int stm = (b.side_to_move == WHITE) ? score : -score;
    return stm + TempoBonus;
}

}  // namespace engine
