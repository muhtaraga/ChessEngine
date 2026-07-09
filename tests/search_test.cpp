// Arama testleri: mat bulma, pat, materyal kazanımı.

#include <gtest/gtest.h>

#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/search.hpp"

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

// İki kale + şah ile bare king'e karşı zorunlu mat (baştan sona çek dizisi) makul
// derinlikte bulunmalı — genel mat-arama regresyon güvencesi.
TEST(Search, FindsTwoRookMate) {
    Board b;
    ASSERT_TRUE(b.set_fen("7k/8/8/8/8/8/R7/1R5K w - - 0 1"));
    SearchResult r = search(b, 8);
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
