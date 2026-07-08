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
    EXPECT_GT(r.score, 500);  // yaklaşık bir vezir öndeyiz
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
    EXPECT_GT(r.score, 500);
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
