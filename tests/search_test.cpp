// Arama testleri: mat bulma, pat, materyal kazanımı.

#include <gtest/gtest.h>

#include "engine/attacks.hpp"
#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/movegen.hpp"
#include "engine/search.hpp"
#include "engine/see.hpp"
#include "engine/tt.hpp"

using namespace engine;

// Mat-in-1: beyaz Ra8# oynamalı (arka sıra matı).
TEST(Search, MateInOne) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 1);
    EXPECT_EQ(r.best, Move::make(A1, A8));
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);          // bizim lehimize mat
    EXPECT_EQ(r.score, MATE - 1);   // 1 ply'de mat
    ASSERT_FALSE(r.pv.empty());     // PV mat hamlesiyle başlamalı
    EXPECT_EQ(r.pv[0], Move::make(A1, A8));
}

// Seldepth: quiescence + extension'larla ulaşılan en derin ply nominal derinliği
// aşmalı (yakalama zincirleri, singular uzatma vb.). Kozmetik alan, aramayı
// değiştirmez; yalnız >= depth invaryantı doğrulanır.
TEST(Search, SeldepthAtLeastDepth) {
    Board b;
    ASSERT_TRUE(b.set_fen(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));  // Kiwipete
    SearchResult r = search(b, 6);
    EXPECT_GE(r.seldepth, 6);  // qsearch nominal derinliğin ötesine iner
}

// Pat: sıradaki tarafın legal hamlesi yok ve çekte değil -> puan 0.
TEST(Search, StalemateIsZero) {
    Board b;
    // Siyah sırada, Kh8; beyaz Qf7, Kg6. Siyahın hamlesi yok, çek yok.
    ASSERT_TRUE(b.set_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    SearchResult r = search(b, 3);
    EXPECT_EQ(r.score, 0);
    EXPECT_FALSE(is_mate_score(r.score));
}

// Bedava vezir: beyaz Rxe5 ile veziri kazanmalı.
TEST(Search, WinsHangingQueen) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 3);
    EXPECT_EQ(r.best, Move::make(E1, E5));
    // Rxe5 sonrası tam bir kale öndeyiz (~500). Tapered eval'de oyun sonunda
    // aktif siyah şah küçük bir aktiflik payı aldığından eşik 400'e çekildi.
    EXPECT_GT(r.score, 400);
}

// Mat-in-2: beyaz 3 ply içinde mat etmeli (puan MATE'e yakın pozitif).
// Boden benzeri basit pozisyon yerine dogrulanabilir bir mat-in-2 kullaniyoruz.
TEST(Search, FindsMateScoreAtDepth) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    // Daha derin aramada da mat puanı korunmalı (mat-in-1 hala en iyi).
    SearchResult r = search(b, 3);
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// Move ordering (MVV-LVA + killer + history) daha derin aramada da doğru
// taktiği bulmalı; sıralama yalnızca hızı etkiler, sonucu değil.
TEST(Search, MoveOrderingPreservesTacticAtDepth) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 5);       // depth 3 yerine 5: sıralama devrede
    EXPECT_EQ(r.best, Move::make(E1, E5));  // yine bedava veziri al
    EXPECT_GT(r.score, 400);  // tam bir kale önde (tapered eval eşiği, bkz. WinsHangingQueen)
}

// Quiescence: horizon effect testi. Beyaz Rxe5 ile piyon "kazanır" gibi görünür
// ama e5 piyonu d6 piyonuyla savunulur -> ...dxe5 kaleyi kazanır. Quiescence
// olmadan derinlik 1'de motor yatay çizginin ardındaki geri alışı göremez ve
// Rxe5'i seçebilirdi; quiescence ile bunu görüp kaçınmalı.
TEST(Search, QuiescenceAvoidsLosingCapture) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/3p4/4p3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 1);
    EXPECT_NE(r.best, Move::make(E1, E5));  // savunmalı piyonu almamalı
    EXPECT_GT(r.score, 0);                  // yine de materyal önde (kale vs 2 piyon)
}

// Iterative deepening + aspiration windows sonucu DEĞİŞTİRMEMELİ (yalnızca hızı
// etkiler): aynı derinlikte sabit-derinlik tam-pencere aramasıyla birebir aynı
// en iyi hamle ve puanı vermeli.
TEST(Search, IterativeMatchesFixedDepth) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchLimits lim;
    lim.max_depth = 6;  // süresiz (soft/hard = -1)
    SearchResult it = search_iterative(b, lim);
    SearchResult fx = search(b, 6);
    EXPECT_EQ(it.best, fx.best);
    EXPECT_EQ(it.score, fx.score);
    EXPECT_EQ(it.best, Move::make(E1, E5));  // yine de bedava veziri al
}

// Iterative deepening mat pozisyonunu bulmalı ve mat puanında erken durmalı.
TEST(Search, IterativeFindsMate) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchLimits lim;
    lim.max_depth = 5;
    SearchResult r = search_iterative(b, lim);
    EXPECT_EQ(r.best, Move::make(A1, A8));
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// Çok kısa süre bütçesinde bile geçerli bir hamle dönmeli: derinlik 1 daima
// süresiz koşar (abort'ta bestmove 0000 dönme regresyonuna karşı).
TEST(Search, IterativeReturnsMoveUnderTightTime) {
    Board b;
    b.set_startpos();
    SearchLimits lim;
    lim.max_depth = 63;
    lim.soft_ms   = 1;
    lim.hard_ms   = 1;
    SearchResult r = search_iterative(b, lim);
    EXPECT_NE(r.best, Move());  // en az derinlik 1'in hamlesi elde olmalı
}

// Adaptif zaman ölçeği: stability arttıkça monoton AZALAN (kararlı -> kıs), sınırlı.
// Yön hatası (sign flip) time-trouble'a yol açar; SPRT bunu "nötr" gösterip sebebi
// gizleyebilir -> burada saf fonksiyonla yakalanır.
TEST(Search, TimeScaleMonotonicAndBounded) {
    double prev = time_scale(0);
    EXPECT_GT(prev, time_scale(8));         // kararlı belirgin daha kısa
    for (int s = 1; s <= 12; ++s) {
        double cur = time_scale(s);
        EXPECT_LE(cur, prev);               // monoton azalan (artmaz)
        EXPECT_GE(cur, 0.5);                // kTimeStabFloor
        EXPECT_LE(cur, 1.5);                // kTimeStabMax
        prev = cur;
    }
    EXPECT_DOUBLE_EQ(time_scale(0), 1.5);   // stability 0 -> en uzun
}

// Uygunluk kapısı: adaptive_time=true bile olsa, zaman sınırı olmayan (soft/hard=-1)
// sabit-derinlik araması BİREBİR değişmez -> ölçekleme yalnız timed-game'de devrede.
// Movetime/depth/infinite modlarının dokunulmazlığının deterministik kanıtı. TT her
// iki koşudan önce temizlenir (soğuk = soğuk) ki düğüm sayısı da birebir kıyaslanabilsin.
TEST(Search, AdaptiveTimeInertWithoutTimeLimit) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchLimits base;
    base.max_depth = 6;  // süresiz
    SearchLimits adapt = base;
    adapt.adaptive_time = true;  // bayrak set ama zaman sınırı yok -> etkisiz olmalı
    TT.clear();
    SearchResult r0 = search_iterative(b, base);
    TT.clear();
    SearchResult r1 = search_iterative(b, adapt);
    EXPECT_EQ(r0.best, r1.best);
    EXPECT_EQ(r0.score, r1.score);
    EXPECT_EQ(r0.nodes, r1.nodes);  // birebir aynı arama
}

// Null move pruning zugzwang'da (yalnız şah+piyon materyali) doğru sonucu
// bozmamalı: has_non_pawn_material guard'ı bu dalda null'ı kapatır. Beyaz
// K+P vs K endgame'inde açıkça kazanan pozisyonda motor pozitif skor + geçerli
// bir hamle bulmalı (null-move blunder regresyonuna karşı).
TEST(Search, NullMoveKeepsZugzwangCorrectness) {
    Board b;
    // Beyaz Ke6 + e5 piyonu (şahla desteklenen, 6. sıraya doğru), siyah şah uzakta.
    // Piyon-dışı materyal yok -> null move guard devrede.
    ASSERT_TRUE(b.set_fen("8/8/4K3/4P3/8/8/8/7k w - - 0 1"));
    SearchResult r = search(b, 12);
    EXPECT_NE(r.best, Move());  // geçerli bir hamle dönmeli
    EXPECT_GT(r.score, 150);    // kazanan pozisyon: skor açıkça pozitif kalmalı
}

// eval>=beta kapısı + dinamik R (null move güçlendirme) aramayı bozmamalı:
// piyon-dışı materyalli (kale -> null aktif) bir pozisyonda bedava vezir hâlâ
// Rxe5 ile alınmalı. Kapı yalnız budamayı isabetlendirir; taktik PVS/re-search
// ile korunur. Gate bug'ı (aramanın yanlışlıkla kapanması) regresyon guard'ı.
TEST(Search, NullMoveGateKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 8);          // null gate + dinamik R belirgin aktif
    EXPECT_EQ(r.best, Move::make(E1, E5));   // bedava veziri al
    EXPECT_GT(r.score, 400);
}

// --- LMR (Late Move Reductions) ---

// LMR aktifken (derinlik 3'ten büyük, geç quiet hamleler indirilir) taktik
// korunmalı: re-search azaltılmış aramada gözden kaçan iyi hamleyi kurtarır.
// Bedava veziri derin aramada da almalı.
TEST(Search, LmrPreservesTacticAtDeepDepth) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 8);         // LMR belirgin aktif
    EXPECT_EQ(r.best, Move::make(E1, E5));  // yine bedava veziri al
    EXPECT_GT(r.score, 400);
}

// LMR azaltmaları mat bulmayı engellememeli: mat-in-1 derin aramada da korunur
// (mat hamlesi çek verir -> indirilmez; ayrıca en iyi sıralanır).
TEST(Search, LmrFindsMateUnderReductions) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 7);
    EXPECT_EQ(r.best, Move::make(A1, A8));
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- Futility ailesi (RFP + futility pruning) ---

// Futility pruning yalnız çek vermeyen QUIET hamleleri budar; yakalamalar
// dokunulmaz. Bedava vezir sığ derinlikte (futility aktif olabilecek yer) hâlâ
// Rxe5 ile alınmalı — kazanan taktik korunur.
TEST(Search, FutilityKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 4);
    EXPECT_EQ(r.best, Move::make(E1, E5));  // yakalama budanmaz
    EXPECT_GT(r.score, 400);
}

// RFP + futility mat aramasını bozmamalı: mat penceresinde devre dışı
// (!is_mate_score guard) ve mat hamlesi çek verdiğinden budanmaz. Mat-in-1 daha
// derin aramada da korunur (regresyon guard'ı; null move testiyle aynı ruhta).
TEST(Search, RfpKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 5);
    EXPECT_EQ(r.best, Move::make(A1, A8));  // arka sıra matı hâlâ bulunur
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- LMP (late move pruning / move-count) ---

// LMP yalnız çek vermeyen QUIET hamleleri (belli sıra numarasından sonra)
// budar; yakalamalar sıra numarasından bağımsız aranır. Bedava vezir sığ
// derinlikte hâlâ Rxe5 ile alınmalı — kazanan taktik korunur.
TEST(Search, LmpKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 4);
    EXPECT_EQ(r.best, Move::make(E1, E5));  // yakalama budanmaz
    EXPECT_GT(r.score, 400);
}

// LMP mat aramasını bozmamalı: mat penceresinde devre dışı (!is_mate_score
// guard) ve mat hamlesi çek verdiğinden budanmaz. Mat-in-1 daha derin aramada
// da korunur (regresyon guard'ı; futility testleriyle aynı ruhta).
TEST(Search, LmpKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 5);
    EXPECT_EQ(r.best, Move::make(A1, A8));  // arka sıra matı hâlâ bulunur
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- Razoring ---

// Razoring qsearch'e düşse bile yakalamalar qsearch'te görülür: bedava vezir
// hâlâ Rxe5 ile alınır (kazanan taktik korunur).
TEST(Search, RazoringKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 4);
    EXPECT_EQ(r.best, Move::make(E1, E5));  // yakalama budanmaz
    EXPECT_GT(r.score, 400);
}

// Razoring mat aramasını bozmamalı: mat penceresinde devre dışı (!is_mate_score)
// ve mat hamlesi qsearch'te de bulunur. Arka sıra matı korunur.
TEST(Search, RazoringKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 5);
    EXPECT_EQ(r.best, Move::make(A1, A8));  // arka sıra matı hâlâ bulunur
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- History-tabanlı quiet budaması ---

// History budaması yalnız çek vermeyen QUIET hamleleri (birleşik history'si çok
// kötü olanları) budar; yakalamalar history sinyalinden bağımsız aranır. Bedava
// vezir sığ derinlikte hâlâ Rxe5 ile alınmalı — kazanan taktik korunur.
TEST(Search, HistPruningKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 4);
    EXPECT_EQ(r.best, Move::make(E1, E5));  // yakalama budanmaz
    EXPECT_GT(r.score, 400);
}

// History budaması mat aramasını bozmamalı: mat penceresinde devre dışı
// (!is_mate_score guard) ve mat hamlesi çek verdiğinden budanmaz. Arka sıra matı
// daha derin aramada da korunur (LMP/futility testleriyle aynı ruhta).
TEST(Search, HistPruningKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 5);
    EXPECT_EQ(r.best, Move::make(A1, A8));  // arka sıra matı hâlâ bulunur
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- Singular extension ---

// Singular extension yalnız derinlik uzatır (depth >= kSingularMinDepth=8), taktiği
// bozmaz: bedava vezir singular-aktif derinlikte hâlâ Rxe5 ile alınır.
TEST(Search, SingularKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 9);        // singular aktif (depth >= 8)
    EXPECT_EQ(r.best, Move::make(E1, E5));
    EXPECT_GT(r.score, 400);
}

// Singular doğrulama araması (tt_move dışlanır, TT'ye yazmaz, tekrar/50-hamle atlar)
// mat aramasını bozmamalı: arka sıra matı derin aramada da korunur.
TEST(Search, SingularKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 9);
    EXPECT_EQ(r.best, Move::make(A1, A8));
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- IIR (Internal Iterative Reduction) ---

// IIR tt_move'suz düğümde derinliği azaltır (depth >= kIirMinDepth=4); taktiği bozmaz:
// bedava vezir hâlâ Rxe5 ile alınır.
TEST(Search, IirKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 6);        // IIR aktif (depth >= 4)
    EXPECT_EQ(r.best, Move::make(E1, E5));
    EXPECT_GT(r.score, 400);
}

// IIR mat aramasını bozmamalı: arka sıra matı azaltılmış derinlikte de bulunur.
TEST(Search, IirKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 6);
    EXPECT_EQ(r.best, Move::make(A1, A8));
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- SEE paketi: kayıplı yakalama sıralaması (Commit 1) ---

// Kayıplı yakalamalar (see<0) artık quiet history bandının ALTINA sıralanıyor, ama
// ASLA budanmıyor. Bu yüzden gerçekte kazanan bir see<0 yakalama hâlâ bulunmalı.
// Burada Qh2xe5: e5 atı yalnız d6 piyonuyla savunulu, ama d6 Bb4 tarafından şaha
// MUTLAK bağlı (pinned). SEE bağı görmez -> dxe5 geri-alışını sayar -> see = 320-900
// = -580 (<0, aşağıda doğrulanır) -> kötü-yakalama bandına iner. Gerçekte d6 oynayamaz,
// vezir atı temiz kazanır (+~1300cp). Sıralama demote'u kazananı kaybettirmemeli.
TEST(Search, SeeOrderingKeepsWinningSacrifice) {
    init_sliding_attacks();  // see() sliding tablolarını ister (idempotent)
    Board b;
    ASSERT_TRUE(b.set_fen("5k2/p6p/3p4/4n3/1B6/8/7Q/6K1 w - - 0 1"));
    const Move win = Move::make(H2, E5);   // bağlı savunucuya rağmen atı al
    EXPECT_LT(see(b, win), 0);             // SEE bunu kötü-yakalama sayar (bağı görmez)
    SearchResult r = search(b, 5);
    EXPECT_EQ(r.best, win);                // demote edildi ama hâlâ bulunur
    EXPECT_GT(r.score, 400);               // açık ara kazanan
}

// --- SEE paketi: sığ-derinlik SEE budaması (Commit 2) ---

// SEE budaması iyi/kazançlı yakalamaları budamaz: eşik negatif, see>=0 daima geçer.
// Bedava vezir Rxe5 (see = +900) sığ derinlikte hâlâ oynanır.
TEST(Search, SeePruningKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 6);
    EXPECT_EQ(r.best, Move::make(E1, E5));  // kazançlı yakalama budanmaz
    EXPECT_GT(r.score, 400);
}

// SEE budaması mat aramasını bozmamalı: mat penceresinde (is_mate_score) kapalı.
// Arka sıra matı hâlâ bulunur.
TEST(Search, SeePruningKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 5);
    EXPECT_EQ(r.best, Move::make(A1, A8));  // arka sıra matı korunur
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- Continuation history ---

// Continuation history yalnızca quiet hamlelerin SIRALAMASINI etkiler; hiçbir
// hamleyi budamaz. Bedava vezir derin aramada da Rxe5 ile alınmalı (sıralama
// değişikliği kazanan taktiği kaybettirmemeli).
TEST(Search, ContHistKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 8);
    EXPECT_EQ(r.best, Move::make(E1, E5));
    EXPECT_GT(r.score, 400);
}

// Continuation history mat aramasını bozmamalı: quiet sıralaması değişse de mat
// hamlesi (çek veren, budanmayan) bulunur. Arka sıra matı derin aramada korunur.
TEST(Search, ContHistKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 7);
    EXPECT_EQ(r.best, Move::make(A1, A8));
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- Countermove (yumuşak history-bonusu) ---

// Countermove bonusu yalnız quiet hamleleri yükseltir; kazançlı yakalamayı
// (bedava vezir Rxe5) bastırmaz. Taktik derin aramada korunur.
TEST(Search, CountermoveKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 8);
    EXPECT_EQ(r.best, Move::make(E1, E5));
    EXPECT_GT(r.score, 400);
}

// Countermove mat aramasını bozmamalı: mat hamlesi çek verir, quiet sıralaması
// bonusundan etkilenmez. Arka sıra matı derin aramada korunur.
TEST(Search, CountermoveKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 7);
    EXPECT_EQ(r.best, Move::make(A1, A8));
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- History-tabanlı LMR ---

// Birleşik history sinyali (main + continuation) indirimi ayarlar: kötü geçmişli
// quiet daha çok indirilir. Aşırı indirim kazanan taktiği kaybettirmemeli —
// yakalamalar zaten LMR görmez, ayrıca re-search kurtarır.
TEST(Search, HistoryLmrKeepsWinningTactic) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 8);
    EXPECT_EQ(r.best, Move::make(E1, E5));
    EXPECT_GT(r.score, 400);
}

// History-LMR mat aramasını bozmamalı: mat hamlesi çek verdiğinden LMR gate'ine
// hiç girmez. Arka sıra matı derin aramada korunur (over-reduction regresyon guard'ı).
TEST(Search, HistoryLmrKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 7);
    EXPECT_EQ(r.best, Move::make(A1, A8));
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// İki kale + şah ile bare king'e karşı zorunlu mat (baştan sona çek dizisi) makul
// derinlikte bulunmalı — genel mat-arama regresyon güvencesi.
TEST(Search, FindsTwoRookMate) {
    Board b;
    ASSERT_TRUE(b.set_fen("7k/8/8/8/8/8/R7/1R5K w - - 0 1"));
    SearchResult r = search(b, 8);
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// Mate distance pruning mat aramasını bozmamalı: arka sıra matı derin aramada
// da doğru hamle/skorla bulunur (MDP yalnız pencereyi mat sınırlarıyla daraltır,
// non-mate sonuçlar aynen korunur — bkz. node-sanity kapısı).
TEST(Search, MateDistancePruningKeepsMate) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 9);
    EXPECT_EQ(r.best, Move::make(A1, A8));  // arka sıra matı hâlâ bulunur
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- Delta pruning (quiescence) ---

// Delta pruning kazançlı yakalamayı budamaz: bedava vezir Rxe5 için üst sınır
// (stand_pat + 900 + marj) alpha'yı fazlasıyla aşar -> aranır. Umutsuz yakalama
// budaması iyi taktiği kaybettirmemeli.
TEST(Search, DeltaPruningKeepsWinningCapture) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/4q3/8/8/8/4RK2 w - - 0 1"));
    SearchResult r = search(b, 6);
    EXPECT_EQ(r.best, Move::make(E1, E5));  // kazançlı yakalama budanmaz
    EXPECT_GT(r.score, 400);
}

// Delta pruning mat aramasını bozmamalı: çekteyken qsearch'in delta dalı hiç
// çalışmaz (stand-pat yok, tüm kaçışlar aranır). Arka sıra matı korunur.
TEST(Search, DeltaPruningKeepsMateSearch) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1"));
    SearchResult r = search(b, 5);
    EXPECT_EQ(r.best, Move::make(A1, A8));  // arka sıra matı korunur
    EXPECT_TRUE(is_mate_score(r.score));
    EXPECT_GT(r.score, 0);
}

// --- Tekrar (repetition) tespiti ---

// Zorunlu tekrar KAYBEDEN tarafı kurtarır (ayırt edici mekanizma testi).
// Beyaz Kh1 çekte (Qh3 h-sütunu), tek legal hamle Kg1. Kg1 sonrası oluşan
// pozisyon (Kg1/Qh3/Ka8, siyah sırası) geçmişte bir kez daha varsa iki-katlıdır
// (twofold) -> arama onu 0 (beraberlik) skorlar. Geçmiş verilmezse aynı hamle
// vezir geride (~ -900) değerlendirilir. Tek fark: history.
TEST(Search, ForcedRepetitionSavesLosingSide) {
    // Kg1 sonrası çocuk pozisyon; anahtarı geçmişe konur (Zobrist halfmove'u
    // içermez, FEN sayaçları anahtarı etkilemez).
    Board child;
    ASSERT_TRUE(child.set_fen("k7/8/8/8/8/7q/8/6K1 b - - 0 1"));
    // is_repetition ply1'de i=4 -> keys[0]'ı yoklar; geçmiş >=3 girişli olmalı
    // ki tarama i=4'e ulaşsın. 1. ve 2. girişler taranmaz (dolgu).
    std::vector<std::uint64_t> hist = {child.key, child.key ^ 1u, child.key ^ 2u};

    Board root;
    // Beyaz Kh1 çekte; halfmove yüksek (tekrar penceresi >=4 ply olsun).
    ASSERT_TRUE(root.set_fen("k7/8/8/8/8/7q/8/7K w - - 20 40"));

    SearchResult with_hist = search(root, 1, -1, hist);
    SearchResult no_hist   = search(root, 1);

    EXPECT_EQ(with_hist.best, Move::make(H1, G1));  // tek legal hamle
    EXPECT_EQ(with_hist.score, 0);                  // zorunlu tekrar -> beraberlik
    EXPECT_LT(no_hist.score, -500);                 // geçmişsiz: vezir geride, kayıp
}

// Kazanan taraf tekrar hattını seçmemeli ve tekrar tespiti kazanç skorunu
// yanlışlıkla 0'a düşürmemeli. Beyaz bir vezir önde; Qa4/a5 + uzak şah sallanması
// köke geri döner (geçmiş tekrarı içerir). Motor sallanma hamlesini oynamamalı,
// skor kazançta kalmalı.
TEST(Search, WinningSideAvoidsRepetition) {
    // Sallanma: Qa4a5 Kh8g8 Qa5a4 Kg8h8 -> başa dön (beyaz sırası). Geçmişi
    // gerçek do_move ile üret (anahtar/parite kendiliğinden doğru).
    Board b;
    ASSERT_TRUE(b.set_fen("7k/8/8/8/Q7/8/8/7K w - - 0 1"));
    std::vector<std::uint64_t> hist;
    const Move seq[] = {Move::make(A4, A5), Move::make(H8, G8),
                        Move::make(A5, A4), Move::make(G8, H8)};
    for (Move m : seq) {
        hist.push_back(b.key);
        b.do_move(m);
    }
    // b artık köke (başlangıç pozisyonuna) eşit, beyaz sırası, bir vezir önde.
    SearchResult r = search(b, 1, -1, hist);
    EXPECT_NE(r.best, Move::make(A4, A5));  // tekrarı (Qa4a5) seçme
    EXPECT_GT(r.score, 800);                // kazanç korunuyor (0'a düşmedi)
}

// --- Düğüm limiti (go nodes N) ---

// Limit uygulanmalı: aramanın harcadığı düğüm bütçeyi anlamlı ölçüde aşmamalı.
// Tolerans neden var: kesme düğüm SAYACINA bakar ama derinlik 1 muaftır (en az bir
// legal hamle garantisi), yani gerçek toplam = derinlik-1 düğümleri + bütçe.
TEST(Search, NodeLimitIsEnforced) {
    Board b;
    ASSERT_TRUE(b.set_fen(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));  // Kiwipete

    SearchLimits lim;
    lim.max_nodes = 20000;
    SearchResult r = search_iterative(b, lim);

    EXPECT_NE(r.best, Move());              // her zaman bir hamle dönmeli
    EXPECT_GT(r.nodes, 0u);
    EXPECT_LT(r.nodes, 60000u);             // bütçenin katbekat üstüne çıkmamalı
}

// Daha büyük bütçe daha çok düğüm (ve en az o kadar derinlik) harcamalı.
TEST(Search, NodeLimitScalesWithBudget) {
    Board b;
    b.set_startpos();

    SearchLimits small;
    small.max_nodes = 5000;
    SearchLimits big;
    big.max_nodes = 200000;

    SearchResult rs = search_iterative(b, small);
    SearchResult rb = search_iterative(b, big);

    EXPECT_LT(rs.nodes, rb.nodes);
}

// Aşırı küçük bütçe bile legal bir hamle vermeli: derinlik 1 limitten muaftır.
// Aksi halde "go nodes 1" -> "bestmove 0000" olurdu (GUI'de kayıp).
TEST(Search, NodeLimitStillReturnsLegalMove) {
    Board b;
    b.set_startpos();

    SearchLimits lim;
    lim.max_nodes = 1;
    SearchResult r = search_iterative(b, lim);

    ASSERT_NE(r.best, Move());
    MoveList ml;
    generate_legal(b, ml);
    bool legal = false;
    for (Move m : ml)
        if (m == r.best) { legal = true; break; }
    EXPECT_TRUE(legal);
}

// max_nodes = 0 (varsayılan) hiçbir şeyi değiştirmemeli: sabit derinlikte arama
// düğüm sayısı birebir aynı kalmalı (davranış-koruyan olduğunun kanıtı).
TEST(Search, NodeLimitZeroIsBehaviorPreserving) {
    Board b;
    ASSERT_TRUE(b.set_fen(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));

    SearchLimits lim;
    lim.max_depth = 8;

    // TT global ve aramalar arası yaşıyor: temizlenmezse ikinci arama birincinin
    // doldurduğu tabloyu bulur ve çok daha az düğümde biter (ölçüldü: 58.261 -> 1.627).
    // Karşılaştırmanın anlamlı olması için ikisi de soğuk tablodan başlamalı.
    TT.clear();
    SearchResult a = search_iterative(b, lim);

    lim.max_nodes = 0;  // açıkça sıfır
    TT.clear();
    SearchResult c = search_iterative(b, lim);

    EXPECT_EQ(a.nodes, c.nodes);
    EXPECT_EQ(a.best, c.best);
    EXPECT_EQ(a.score, c.score);
}
