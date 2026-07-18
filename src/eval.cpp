// Statik değerlendirme implementasyonu (tapered eval: MG/EG interpolasyonu).

#include "engine/eval.hpp"

#include "engine/attacks.hpp"
#include "engine/bitboard.hpp"
#include "engine/board.hpp"
#include "engine/pawn_table.hpp"

namespace engine {

// Pawn hash cache anahtarı: arama sırasında true (pawn_structure sonucu
// PAWN_TABLE'da memoize edilir). Tuner FALSE yapar: finite-difference pawn
// ağırlıklarını perturbe edip eval_accumulate çağırıyor; cache açıkken perturbe
// edilmiş param için BAYAT değer döner -> pawn gradyanları ~0 -> hiç tune edilmez.
bool g_pawn_cache_enabled = true;

EvalParams make_default_eval_params() {
    EvalParams p{};
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        p.material[pt]     = MaterialValue[pt];
        p.mobility_mg[pt]  = MobilityMg[pt];
        p.mobility_eg[pt]  = MobilityEg[pt];
        p.king_attack_weight[pt] = KingAttackWeight[pt];
        for (int sq = 0; sq < SQUARE_NB; ++sq) {
            p.pst_mg[pt][sq] = PstMg[pt][sq];
            p.pst_eg[pt][sq] = PstEg[pt][sq];
        }
    }
    p.isolated_mg = IsolatedPenaltyMg; p.isolated_eg = IsolatedPenaltyEg;
    p.doubled_mg  = DoubledPenaltyMg;  p.doubled_eg  = DoubledPenaltyEg;
    for (int i = 0; i < 8; ++i) {
        p.passed_mg[i] = PassedBonusMg[i];
        p.passed_eg[i] = PassedBonusEg[i];
    }
    p.bishop_pair_mg = BishopPairMg; p.bishop_pair_eg = BishopPairEg;
    p.rook_open_mg   = RookOpenMg;   p.rook_open_eg   = RookOpenEg;
    p.rook_semi_mg   = RookSemiMg;   p.rook_semi_eg   = RookSemiEg;
    p.threat_by_pawn_mg  = ThreatByPawnMg;  p.threat_by_pawn_eg  = ThreatByPawnEg;
    p.threat_by_minor_mg = ThreatByMinorMg; p.threat_by_minor_eg = ThreatByMinorEg;
    p.threat_by_rook_mg  = ThreatByRookMg;  p.threat_by_rook_eg  = ThreatByRookEg;
    p.hanging_mg         = HangingMg;        p.hanging_eg         = HangingEg;
    p.outpost_knight_mg  = OutpostKnightMg;  p.outpost_knight_eg  = OutpostKnightEg;
    p.outpost_bishop_mg  = OutpostBishopMg;  p.outpost_bishop_eg  = OutpostBishopEg;
    p.passer_king_escort_eg = PasserKingEscortEg;
    p.rook_behind_passer_eg = RookBehindPasserEg;
    p.bad_bishop_mg         = BadBishopMg;         p.bad_bishop_eg         = BadBishopEg;
    p.bad_bishop_blocked_mg = BadBishopBlockedMg;  p.bad_bishop_blocked_eg = BadBishopBlockedEg;
    p.backward_mg           = BackwardPenaltyMg;   p.backward_eg           = BackwardPenaltyEg;
    p.shield_missing = ShieldMissingPenalty;
    for (int i = 0; i < 100; ++i)
        p.safety_table[i] = SafetyTable[i];
    return p;
}

// Global tunable eval parametreleri; varsayılan (elle-seçilmiş) ağırlıklarla başlar.
// Kaynaklar constexpr olduğundan statik-init sırası sorunu yok.
EvalParams g_eval = make_default_eval_params();

int game_phase(const Board& b) {
    int phase = 0;
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        phase += PhaseWeight[pt] * popcount(b.pieces[pt]);
    // Erken promosyonla (ör. iki vezir) faz teorik tavanı aşabilir; tavanla sınırla.
    return phase > MAX_PHASE ? MAX_PHASE : phase;
}

// Bir rengin tüm piyon vuruş karelerinin birleşimi (LERF yön shift'leri; kenar
// sütun taşmasını FileMask ile önler). Beyaz yukarı-çapraz (<<7/<<9), siyah
// aşağı-çapraz (>>9/>>7) vurur.
Bitboard pawn_attack_span(Bitboard pawns, Color c) {
    const Bitboard not_a = ~FileMask[0];
    const Bitboard not_h = ~FileMask[7];
    if (c == WHITE)
        return ((pawns & not_a) << 7) | ((pawns & not_h) << 9);
    return ((pawns & not_a) >> 9) | ((pawns & not_h) >> 7);
}

void pawn_structure(const Board& b, int& mg, int& eg) {
    // İzole test için ince sarmalayıcı; geçer piyon kümesi atılır.
    Bitboard pw = 0, pb = 0;
    pawn_structure_full(b, mg, eg, pw, pb);
}

void backward_pawns(const Board& b, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    const Bitboard wp   = b.pieces[PAWN] & b.colors[WHITE];
    const Bitboard bp   = b.pieces[PAWN] & b.colors[BLACK];
    const Bitboard watt = pawn_attack_span(wp, WHITE);  // beyaz piyonların vurduğu kareler
    const Bitboard batt = pawn_attack_span(bp, BLACK);  // siyah piyonların vurduğu kareler

    // Beyaz geri piyon: komşu sütunda dost piyon VAR ama hepsi İLERİDE (geriden
    // destek yok), VE durak karesi (s+8) bir siyah piyonca kontrol ediliyor.
    Bitboard w = wp;
    while (w) {
        Square   sq         = pop_lsb(w);
        Bitboard neighbours = wp & AdjacentFileMask[file_of(sq)];
        if (!neighbours) continue;                      // komşu yok -> izole (ayrı terim)
        // PassedMask[WHITE][sq] = komşu+kendi sütun, sıra > r. neighbours zaten yalnız
        // komşu sütunda -> onunla kesişim "ileride" komşular. ~ile "geride/hizada" olanlar;
        // varsa geriden desteklenebilir -> geri DEĞİL.
        if (neighbours & ~PassedMask[WHITE][sq]) continue;
        if (test_bit(batt, Square(sq + 8))) {           // durak karesi siyah piyon kontrolünde
            mg += g_eval.backward_mg;
            eg += g_eval.backward_eg;
        }
    }

    // Siyah geri piyon (ayna -> beyaz bakışına eksi). Siyah için "ileri" = düşük sıra;
    // PassedMask[BLACK][sq] = sıra < r. Durak karesi s-8, beyaz piyonca kontrol.
    Bitboard bb2 = bp;
    while (bb2) {
        Square   sq         = pop_lsb(bb2);
        Bitboard neighbours = bp & AdjacentFileMask[file_of(sq)];
        if (!neighbours) continue;
        if (neighbours & ~PassedMask[BLACK][sq]) continue;
        if (test_bit(watt, Square(sq - 8))) {
            mg -= g_eval.backward_mg;
            eg -= g_eval.backward_eg;
        }
    }
}

void pawn_structure_full(const Board& b, int& mg, int& eg,
                         Bitboard& passed_w, Bitboard& passed_b) {
    mg = 0;
    eg = 0;
    passed_w = 0;
    passed_b = 0;

    const Bitboard wp = b.pieces[PAWN] & b.colors[WHITE];
    const Bitboard bp = b.pieces[PAWN] & b.colors[BLACK];

    // --- Çift piyon (doubled): sütun başına sayılır (piyon başına değil, çift
    // sayımı önlemek için). Bir sütunda k piyon varsa (k-1) fazlalık cezalanır.
    for (int f = 0; f < 8; ++f) {
        int wc = popcount(wp & FileMask[f]);
        if (wc > 1) {
            mg += (wc - 1) * g_eval.doubled_mg;
            eg += (wc - 1) * g_eval.doubled_eg;
        }
        int bc = popcount(bp & FileMask[f]);
        if (bc > 1) {  // siyahın cezası beyaz bakışına + olarak yansır
            mg -= (bc - 1) * g_eval.doubled_mg;
            eg -= (bc - 1) * g_eval.doubled_eg;
        }
    }

    // --- İzole + geçer piyon: piyon başına.
    Bitboard w = wp;
    while (w) {
        Square sq = pop_lsb(w);
        if ((wp & AdjacentFileMask[file_of(sq)]) == 0) {  // izole
            mg += g_eval.isolated_mg;
            eg += g_eval.isolated_eg;
        }
        if ((bp & PassedMask[WHITE][sq]) == 0) {  // geçer (önünde rakip piyon yok)
            int r = rank_of(sq);
            mg += g_eval.passed_mg[r];
            eg += g_eval.passed_eg[r];
            passed_w |= square_bb(sq);
        }
    }

    Bitboard bb = bp;
    while (bb) {
        Square sq = pop_lsb(bb);
        if ((bp & AdjacentFileMask[file_of(sq)]) == 0) {  // siyah izole -> beyaz +
            mg -= g_eval.isolated_mg;
            eg -= g_eval.isolated_eg;
        }
        if ((wp & PassedMask[BLACK][sq]) == 0) {  // siyah geçer -> beyaz −
            int r = 7 - rank_of(sq);  // siyah için sıra aynalanır
            mg -= g_eval.passed_mg[r];
            eg -= g_eval.passed_eg[r];
            passed_b |= square_bb(sq);
        }
    }

    // Geri piyon (saf-piyon -> cache'e giren pawn_structure sonucuna dahil).
    int bwd_mg = 0, bwd_eg = 0;
    backward_pawns(b, bwd_mg, bwd_eg);
    mg += bwd_mg;
    eg += bwd_eg;
}

namespace {

// mobility, king_safety ve threats AYNI taş-atak setlerini (at/fil/kale/vezir)
// paylaşır: mobility kendi renginin taşları için, king_safety rakip şaha saldıran
// aynı taşlar için, threats de aynı atak setlerini rakip taşlarla kesiştirir. Bu tek
// geçiş her taşın atak setini BİR KEZ hesaplar ve üç katkıyı da toplar -> yalnız
// sliding-magic çağrıları bir kez. (mobility+king_safety kısmı E1'de davranış-koruyan
// birleştirildi; threats E2'de eklendi -> eval'i değiştirir, davranış-koruyan DEĞİL.)
struct AttackEval {
    int mob_mg = 0;
    int mob_eg = 0;
    int ks_mg  = 0;  // king safety (eg her zaman 0; taper ile EG'de solar)
    int thr_mg = 0;  // threats / hanging (tapered: mg/eg ayrı)
    int thr_eg = 0;
    int out_mg = 0;  // outpost (desteklenen + kovulamayan ileri at/fil; tapered)
    int out_eg = 0;
    int bb_mg  = 0;  // kötü fil (fil-renginde dost piyonlar + blokeli ek; tapered)
    int bb_eg  = 0;
};

AttackEval attack_eval_impl(const Board& b) {
    AttackEval r;
    const Bitboard occ = b.occupancy();

    // Şah bölgeleri (king ring): beyaz şaha SİYAH taşlar, siyah şaha BEYAZ taşlar
    // saldırır. Tehlike birimleri (units) her şah için ayrı biriktirilir.
    const Square   wksq        = b.king_square(WHITE);
    const Square   bksq        = b.king_square(BLACK);
    const Bitboard wzone       = king_attacks(wksq);
    const Bitboard bzone       = king_attacks(bksq);
    int            units_white = 0;  // beyaz şaha karşı (siyah saldıranlar)
    int            units_black = 0;  // siyah şaha karşı (beyaz saldıranlar)

    // Threats için renk başına toplam atak setleri (birleşik geçişte doldurulur).
    // by_pawn: piyon vuruşları; by_minor: at|fil; by_rook: kale; by_piece: tüm
    // N/B/R/Q. by_all (piyon|piece|şah) hanging'in "savunma/saldırı" testinde kullanılır.
    Bitboard by_pawn[COLOR_NB]  = {pawn_attack_span(b.pieces[PAWN] & b.colors[WHITE], WHITE),
                                   pawn_attack_span(b.pieces[PAWN] & b.colors[BLACK], BLACK)};
    Bitboard by_minor[COLOR_NB] = {0, 0};
    Bitboard by_rook[COLOR_NB]  = {0, 0};
    Bitboard by_piece[COLOR_NB] = {0, 0};

    // --- Mobility + king-ring saldırıları: taş başına tek atak seti ---
    for (Color c : {WHITE, BLACK}) {
        const Bitboard own             = b.colors[c];
        const int      sign            = (c == WHITE) ? 1 : -1;
        const Bitboard enemy_king_zone = (c == WHITE) ? bzone : wzone;
        const Bitboard enemy_pawns     = b.pieces[PAWN] & b.colors[~c];
        int&           enemy_units     = (c == WHITE) ? units_black : units_white;

        auto add = [&](PieceType pt, Bitboard attacks) {
            // mobility: dost taşla dolu kareler hariç ulaşılabilir kare sayısı
            int m = popcount(attacks & ~own);
            r.mob_mg += sign * m * g_eval.mobility_mg[pt];
            r.mob_eg += sign * m * g_eval.mobility_eg[pt];
            // king safety: bu taş rakip şah bölgesinde kaç kare vuruyor × ağırlık
            enemy_units += g_eval.king_attack_weight[pt] * popcount(attacks & enemy_king_zone);
            // threats: atak setini tür-bazlı aggregate'lere ekle
            by_piece[c] |= attacks;
            if (pt == KNIGHT || pt == BISHOP) by_minor[c] |= attacks;
            else if (pt == ROOK)              by_rook[c]  |= attacks;
        };

        // Outpost: dost piyonla DESTEKLENEN + rakip piyonun KOVAMAYACAĞI ileri karedeki
        // at/fil. `add`'den ayrı tutuldu: `add`'in işi "bu atak setini aggregate'e ekle",
        // outpost ise bir YERLEŞİM terimi (sıfır atak biti okur) -> `add`'e kare parametresi
        // eklemek kale/veziri ölü argümanla kirletirdi.
        auto add_outpost = [&](PieceType pt, Square s) {
            const int rr = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            if (rr < 3 || rr > 5) return;          // göreli sıra 4-6 dışı
            if (!test_bit(by_pawn[c], s)) return;  // dost piyon desteklemiyor

            // Kovulabilirlik: s'yi bir gün vurabilecek rakip piyonun durabileceği kareler
            // = KOMŞU sütunlar × s'nin ÖNÜ (rakip piyon yalnız bize doğru ilerler, geri
            // dönemez). PassedMask komşu + KENDİ sütununu "kesin önde" verir;
            // AdjacentFileMask kendi sütununu eler -> tam olarak o küme. Yeni tablo yok.
            //
            // NOT: "kovamaz" = KENDİ SÜTUNUNDA İLERLEYEREK kovamaz. Rakip piyon vuruş
            // yapıp sütun değiştirerek sonradan kovabilir (d7 piyonu c6'ya vurup d5'i
            // tehdit eder). Stockfish'in pawn_attacks_span tanımı da vuruşları saymaz —
            // bilinçli sadeleştirme.
            if ((PassedMask[c][s] & AdjacentFileMask[file_of(s)] & enemy_pawns) != 0)
                return;

            r.out_mg += sign * (pt == KNIGHT ? g_eval.outpost_knight_mg
                                             : g_eval.outpost_bishop_mg);
            r.out_eg += sign * (pt == KNIGHT ? g_eval.outpost_knight_eg
                                             : g_eval.outpost_bishop_eg);
        };

        // Kötü fil: filin durduğu karenin RENGİNDEKİ dost piyonlar. Taban ceza piyon
        // başına; BLOKELİ olanlara (önündeki kare -renk yönünde- dolu) ek ceza (mobility'nin
        // hafife aldığı kalıcı yapısal zaafiyet). Ceza o rengin skorunu düşürür.
        const Bitboard own_pawns = b.pieces[PAWN] & own;
        auto add_bad_bishop = [&](Square s) {
            const Bitboard color_mask = SquareColorMask[(file_of(s) + rank_of(s)) & 1];
            const Bitboard same       = own_pawns & color_mask;
            if (!same) return;
            // Bloke: piyonun önündeki kare (beyaz +8, siyah −8) herhangi bir taşla dolu.
            // occ'u ilerleme yönünün TERSİNE kaydır -> dolu-önlü piyon bitleri hizalanır.
            const Bitboard blocked = same & (c == WHITE ? (occ >> 8) : (occ << 8));
            const int pen_mg = g_eval.bad_bishop_mg * popcount(same)
                             + g_eval.bad_bishop_blocked_mg * popcount(blocked);
            const int pen_eg = g_eval.bad_bishop_eg * popcount(same)
                             + g_eval.bad_bishop_blocked_eg * popcount(blocked);
            r.bb_mg += -sign * pen_mg;
            r.bb_eg += -sign * pen_eg;
        };

        Bitboard knights = b.pieces[KNIGHT] & own;
        while (knights) { Square s = pop_lsb(knights); add(KNIGHT, knight_attacks(s)); add_outpost(KNIGHT, s); }

        Bitboard bishops = b.pieces[BISHOP] & own;
        while (bishops) { Square s = pop_lsb(bishops); add(BISHOP, bishop_attacks(s, occ)); add_outpost(BISHOP, s); add_bad_bishop(s); }

        Bitboard rooks = b.pieces[ROOK] & own;
        while (rooks) { Square s = pop_lsb(rooks); add(ROOK, rook_attacks(s, occ)); }

        Bitboard queens = b.pieces[QUEEN] & own;
        while (queens) { Square s = pop_lsb(queens); add(QUEEN, queen_attacks(s, occ)); }
    }

    // --- Piyon kalkanı + SafetyTable: her şah için (atak setlerinden bağımsız) ---
    for (Color c : {WHITE, BLACK}) {
        const int    sign = (c == WHITE) ? 1 : -1;
        const Square ksq  = b.king_square(c);
        const int    kf   = file_of(ksq);
        const int    kr   = rank_of(ksq);
        int          danger = 0;

        // Piyon kalkanı: şahın önündeki iki sıra, kf-1..kf+1 sütunları.
        const Bitboard own_pawns = b.pieces[PAWN] & b.colors[c];
        for (int f = kf - 1; f <= kf + 1; ++f) {
            if (f < 0 || f > 7) continue;  // tahta kenarı
            Bitboard front = 0;
            for (int step = 1; step <= 2; ++step) {
                int rr = (c == WHITE) ? kr + step : kr - step;
                if (rr < 0 || rr > 7) continue;
                front |= Bitboard{1} << (rr * 8 + f);
            }
            if ((own_pawns & front) == 0)  // bu sütunda kalkan piyonu yok
                danger += g_eval.shield_missing;
        }

        int units = (c == WHITE) ? units_white : units_black;
        if (units > 99) units = 99;  // tablo indeksini kırp
        danger += g_eval.safety_table[units];

        r.ks_mg += -sign * danger;  // tehlike, o rengin skorunu düşürür
    }

    // --- Threats / hanging: paylaşılan atak setlerini rakip taşlarla kesiştir ---
    // by_all: bir rengin herhangi bir taşının (piyon|N/B/R/Q|şah) vurduğu kareler;
    // hanging'in "vurulmuş mu / savunuluyor mu" testinde kullanılır.
    const Bitboard by_all[COLOR_NB] = {
        by_pawn[WHITE] | by_piece[WHITE] | king_attacks(wksq),
        by_pawn[BLACK] | by_piece[BLACK] | king_attacks(bksq)};

    for (Color us : {WHITE, BLACK}) {
        const Color    them = ~us;
        const int      sign = (us == WHITE) ? 1 : -1;
        const Bitboard enemy = b.colors[them];
        const Bitboard enemy_majors = (b.pieces[ROOK] | b.pieces[QUEEN]) & enemy;
        const Bitboard enemy_queens = b.pieces[QUEEN] & enemy;
        // Tehdit/hanging yalnız N/B/R/Q için (piyon ve şah hariç).
        const Bitboard enemy_pieces =
            (b.pieces[KNIGHT] | b.pieces[BISHOP] | b.pieces[ROOK] | b.pieces[QUEEN]) & enemy;

        // Piyon vuruşumuz altındaki rakip taş.
        int n = popcount(by_pawn[us] & enemy_pieces);
        r.thr_mg += sign * n * g_eval.threat_by_pawn_mg;
        r.thr_eg += sign * n * g_eval.threat_by_pawn_eg;

        // Minör (at/fil) atağımız altındaki rakip majör (kale/vezir).
        n = popcount(by_minor[us] & enemy_majors);
        r.thr_mg += sign * n * g_eval.threat_by_minor_mg;
        r.thr_eg += sign * n * g_eval.threat_by_minor_eg;

        // Kale atağımız altındaki rakip vezir.
        n = popcount(by_rook[us] & enemy_queens);
        r.thr_mg += sign * n * g_eval.threat_by_rook_mg;
        r.thr_eg += sign * n * g_eval.threat_by_rook_eg;

        // Hanging: bizim vurduğumuz ama rakibin hiçbir taşınca savunmadığı rakip taş.
        n = popcount(enemy_pieces & by_all[us] & ~by_all[them]);
        r.thr_mg += sign * n * g_eval.hanging_mg;
        r.thr_eg += sign * n * g_eval.hanging_eg;
    }

    return r;
}

}  // namespace

void mobility(const Board& b, int& mg, int& eg) {
    // İzole test için ince sarmalayıcı; gerçek hesap tek geçişli impl'de.
    AttackEval r = attack_eval_impl(b);
    mg = r.mob_mg;
    eg = r.mob_eg;
}


namespace {

// Şah mesafesi (Chebyshev / king-move mesafesi): iki kare arası şahın kaç hamlede
// gideceği = max(|sütun farkı|, |sıra farkı|). Projede böyle bir yardımcı yoktu.
int king_distance(Square a, Square b) {
    int df = file_of(a) - file_of(b);
    int dr = rank_of(a) - rank_of(b);
    if (df < 0) df = -df;
    if (dr < 0) dr = -dr;
    return df > dr ? df : dr;
}

// Mesafe kırpma sınırı: 5'ten öteye pratikte fark yok (şah zaten yetişemez) ve terimi
// sınırlar (Stockfish king_proximity de min(dist,5) yapar).
constexpr int kKingDistCap = 5;

}  // namespace

void passer_king_escort_with(const Board& b, Bitboard passed_w, Bitboard passed_b,
                             int& mg, int& eg) {
    mg = 0;  // YALNIZ oyun sonu terimi (taper ile orta oyunda solar)
    eg = 0;

    const Square wk = b.king_square(WHITE);
    const Square bk = b.king_square(BLACK);

    auto cap = [](int d) { return d > kKingDistCap ? kKingDistCap : d; };

    // Beyaz geçer piyonu: durak karesi s+8 (piyon 8. sırada olamaz -> tahta içinde).
    // Yalnız KENDİ şahımız rakip şahtan daha yakınsa bonus (max(0,...)): normal durumda
    // ceza YOK -> terim passed[r] ile kavga edemez (bkz. eval.hpp tasarım notu).
    Bitboard w = passed_w;
    while (w) {
        Square s = pop_lsb(w);
        if (rank_of(s) < 3) continue;  // göreli sıra < 4 -> şah mesafesi henüz anlamsız
        Square stop = static_cast<Square>(static_cast<int>(s) + 8);
        int    diff = cap(king_distance(bk, stop)) - cap(king_distance(wk, stop));
        if (diff > 0) eg += g_eval.passer_king_escort_eg * diff;
    }

    // Siyah geçer piyonu: durak karesi s-8; göreli sıra 7 - rank_of(s).
    Bitboard bb = passed_b;
    while (bb) {
        Square s = pop_lsb(bb);
        if (7 - rank_of(s) < 3) continue;
        Square stop = static_cast<Square>(static_cast<int>(s) - 8);
        int    diff = cap(king_distance(wk, stop)) - cap(king_distance(bk, stop));
        if (diff > 0) eg -= g_eval.passer_king_escort_eg * diff;
    }
}

void passer_king_escort(const Board& b, int& mg, int& eg) {
    // İzole test için ince sarmalayıcı: kümeyi kendisi üretir (aramada bu yol
    // KULLANILMAZ — orada küme pawn cache'ten gelir).
    int      pmg = 0, peg = 0;
    Bitboard pw = 0, pb = 0;
    pawn_structure_full(b, pmg, peg, pw, pb);
    passer_king_escort_with(b, pw, pb, mg, eg);
}

void rook_behind_passer_with(const Board& b, Bitboard passed_w, Bitboard passed_b,
                             int& mg, int& eg) {
    mg = 0;  // YALNIZ oyun sonu terimi (taper ile orta oyunda solar)
    eg = 0;

    const Bitboard occ = b.occupancy();
    const Bitboard wr  = b.pieces[ROOK] & b.colors[WHITE];
    const Bitboard br  = b.pieces[ROOK] & b.colors[BLACK];

    // Passer başına: aynı sütunda, ARKADA, arada taş OLMADAN dost kale var mı?
    // "Arkada" = piyonun ilerleyiş yönünün TERSİ (beyaz: daha düşük sıra; siyah:
    // daha yüksek sıra). between_bb temiz-hat testini verir -> istiflenmiş kalede
    // yalnız yakın olan (arası boş) sayılır, passer başına en fazla bir bonus.
    Bitboard w = passed_w;
    while (w) {
        Square s = pop_lsb(w);
        if (rank_of(s) < 3) continue;  // göreli sıra < 4 -> kale-arkada marjinal
        Bitboard rooks = wr & FileMask[file_of(s)];
        while (rooks) {
            Square r = pop_lsb(rooks);
            if (rank_of(r) >= rank_of(s)) continue;      // önde ya da aynı -> arkada değil
            if ((between_bb(r, s) & occ) == 0) {         // temiz hat
                eg += g_eval.rook_behind_passer_eg;
                break;                                   // passer başına bir kez
            }
        }
    }

    Bitboard bb = passed_b;
    while (bb) {
        Square s = pop_lsb(bb);
        if (7 - rank_of(s) < 3) continue;
        Bitboard rooks = br & FileMask[file_of(s)];
        while (rooks) {
            Square r = pop_lsb(rooks);
            if (rank_of(r) <= rank_of(s)) continue;      // siyah "arkada" = daha yüksek sıra
            if ((between_bb(r, s) & occ) == 0) {
                eg -= g_eval.rook_behind_passer_eg;
                break;
            }
        }
    }
}

void rook_behind_passer(const Board& b, int& mg, int& eg) {
    // İzole test için ince sarmalayıcı: kümeyi kendisi üretir (aramada bu yol
    // KULLANILMAZ — orada küme pawn cache'ten gelir).
    int      pmg = 0, peg = 0;
    Bitboard pw = 0, pb = 0;
    pawn_structure_full(b, pmg, peg, pw, pb);
    rook_behind_passer_with(b, pw, pb, mg, eg);
}

void bishop_pair(const Board& b, int& mg, int& eg) {
    mg = 0;
    eg = 0;
    // İki (veya daha fazla) fili olan tarafa bonus. Basit ≥2 sayımı standart;
    // zıt-kare kontrolü (nadir aynı-renk çift promosyon) bilinçli ertelendi.
    if (popcount(b.pieces[BISHOP] & b.colors[WHITE]) >= 2) { mg += g_eval.bishop_pair_mg; eg += g_eval.bishop_pair_eg; }
    if (popcount(b.pieces[BISHOP] & b.colors[BLACK]) >= 2) { mg -= g_eval.bishop_pair_mg; eg -= g_eval.bishop_pair_eg; }
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
                    mg += sign * g_eval.rook_open_mg;
                    eg += sign * g_eval.rook_open_eg;
                } else {  // yalnız rakip piyon -> yarı-açık sütun
                    mg += sign * g_eval.rook_semi_mg;
                    eg += sign * g_eval.rook_semi_eg;
                }
            }
        }
    }
}

void king_safety(const Board& b, int& mg, int& eg) {
    // İzole test için ince sarmalayıcı; gerçek hesap tek geçişli impl'de
    // (mobility ile paylaşılan taş-atak setleri). eg her zaman 0.
    AttackEval r = attack_eval_impl(b);
    mg = r.ks_mg;
    eg = 0;
}

void threats(const Board& b, int& mg, int& eg) {
    // İzole test için ince sarmalayıcı; gerçek hesap tek geçişli impl'de
    // (mobility/king_safety ile paylaşılan taş-atak setleri).
    AttackEval r = attack_eval_impl(b);
    mg = r.thr_mg;
    eg = r.thr_eg;
}

void outpost(const Board& b, int& mg, int& eg) {
    // İzole test için ince sarmalayıcı; gerçek hesap tek geçişli impl'de
    // (at/fil döngüleri mobility ile paylaşılır).
    AttackEval r = attack_eval_impl(b);
    mg = r.out_mg;
    eg = r.out_eg;
}

void bad_bishop(const Board& b, int& mg, int& eg) {
    // İzole test için ince sarmalayıcı; gerçek hesap tek geçişli impl'de
    // (fil döngüsü mobility/outpost ile paylaşılır).
    AttackEval r = attack_eval_impl(b);
    mg = r.bb_mg;
    eg = r.bb_eg;
}

void eval_accumulate(const Board& b, int& mg_white, int& eg_white) {
    // Orta oyun ve oyun sonu puanları ayrı biriktirilir (beyaz bakışıyla).
    int mg = 0;
    int eg = 0;

    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        int material = g_eval.material[pt];  // faz-bağımsız (MG = EG)

        // Beyaz taşlar: +materyal +PST
        Bitboard wbb = b.pieces[pt] & b.colors[WHITE];
        while (wbb) {
            Square sq = pop_lsb(wbb);
            mg += material + g_eval.pst_mg[pt][sq];
            eg += material + g_eval.pst_eg[pt][sq];
        }

        // Siyah taşlar: -materyal -PST (dikey ayna: sq ^ 56)
        Bitboard bbb = b.pieces[pt] & b.colors[BLACK];
        while (bbb) {
            Square sq = static_cast<Square>(static_cast<int>(pop_lsb(bbb)) ^ 56);
            mg -= material + g_eval.pst_mg[pt][sq];
            eg -= material + g_eval.pst_eg[pt][sq];
        }
    }

    // Pawn structure (izole/çift/geçer) katkısı beyaz − siyah olarak eklenir.
    // Pawn hash cache: pawn_key ile memoize (saf fonksiyon -> EXACT). Miss'te
    // ham hesap + store. Tuner cache'i kapatır (bkz. g_pawn_cache_enabled).
    // Geçer piyon KÜMELERİ de cache'te taşınır (küme piyon-saf): blokaj gibi passer
    // rafineleri kümeyi buradan alır, yeniden üretmez (yeniden üretim ölçüldü: %3-5 nps).
    int      pmg = 0, peg = 0;
    Bitboard passed_w = 0, passed_b = 0;
    if (g_pawn_cache_enabled && PAWN_TABLE.probe(b.pawn_key, pmg, peg, passed_w, passed_b)) {
        // cache hit
    } else {
        pawn_structure_full(b, pmg, peg, passed_w, passed_b);
        if (g_pawn_cache_enabled) PAWN_TABLE.store(b.pawn_key, pmg, peg, passed_w, passed_b);
    }
    mg += pmg;
    eg += peg;

    // Mobility + king safety + threats + outpost + bad bishop TEK GEÇİŞTE (paylaşılan
    // at/fil/kale/vezir atak setleri yeniden üretilmez). mob_mg/eg mobility, ks_mg king
    // safety (eg 0), thr_mg/eg threats, out_mg/eg outpost, bb_mg/eg kötü fil. Ayrı ayrı
    // wrapper çağırmakla BİREBİR aynı toplam.
    AttackEval aks = attack_eval_impl(b);
    mg += aks.mob_mg + aks.ks_mg + aks.thr_mg + aks.out_mg + aks.bb_mg;
    eg += aks.mob_eg + aks.thr_eg + aks.out_eg + aks.bb_eg;

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

    // Geçer piyon şah eskortu: terim şah yerine bağlı (cache'e giremez) ama geçer
    // piyon kümesi yukarıda cache'ten geldi -> yeniden üretim yok. mg katkısı hep 0.
    int kdmg = 0, kdeg = 0;
    passer_king_escort_with(b, passed_w, passed_b, kdmg, kdeg);
    eg += kdeg;

    // Kale kendi geçer piyonunun arkasında: escort gibi kaleye bağlı (cache'e giremez)
    // ama geçer piyon kümesi yukarıda cache'ten geldi -> yeniden üretim yok. mg katkısı hep 0.
    int rbmg = 0, rbeg = 0;
    rook_behind_passer_with(b, passed_w, passed_b, rbmg, rbeg);
    eg += rbeg;

    mg_white = mg;
    eg_white = eg;
}

int evaluate(const Board& b) {
    int mg = 0, eg = 0;
    eval_accumulate(b, mg, eg);

    // Faza göre interpolasyon: tam kadroda (phase=MAX) tamamen mg, oyun sonunda
    // (phase=0) tamamen eg. Materyal her iki uçta eşit olduğundan interpolasyondan
    // etkilenmez; yalnızca faza duyarlı PST (şu an şah) + pawn structure fazla göre kayar.
    int phase = game_phase(b);
    int score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    // Hamle sırası olan tarafın bakış açısına çevir.
    return (b.side_to_move == WHITE) ? score : -score;
}

}  // namespace engine
