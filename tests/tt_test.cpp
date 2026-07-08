// Transposition table testleri.
//
// İki düzey: (1) TT'nin birim davranışı (store/probe/clear/değiştirme),
// (2) TT'nin aramayı bozmadığı — sonuç (skor + en iyi hamle) TT açıkken de
// TT kapalı referansla aynı olmalı; ayrıca doldurulmuş TT yeniden aramada
// düğüm sayısını düşürmeli (asıl amacı).

#include <cstdint>

#include <gtest/gtest.h>

#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/search.hpp"
#include "engine/tt.hpp"

using namespace engine;

// --- Birim: store + probe roundtrip ---
TEST(TT, StoreProbeRoundtrip) {
    TT.clear();
    const std::uint64_t key = 0xDEADBEEFCAFEBABEULL;
    Move mv = Move::make(E2, E4);
    TT.store(key, /*depth=*/7, /*score=*/123, Bound::EXACT, mv);

    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.key, key);
    EXPECT_EQ(e.depth, 7);
    EXPECT_EQ(e.score, 123);
    EXPECT_EQ(e.bound, Bound::EXACT);
    EXPECT_EQ(e.move, mv);
}

// --- Birim: bilinmeyen anahtar sondası boş döner ---
TEST(TT, ProbeMissOnUnknownKey) {
    TT.clear();
    TT.store(0x1111111111111111ULL, 4, 10, Bound::LOWER, Move::make(A2, A3));
    TTEntry e;
    EXPECT_FALSE(TT.probe(0x2222222222222222ULL, e));
}

// --- Birim: clear tüm girişleri siler ---
TEST(TT, ClearEmpties) {
    const std::uint64_t key = 0xABCDEF0123456789ULL;
    TT.store(key, 3, 5, Bound::UPPER, Move::make(D2, D4));
    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    TT.clear();
    EXPECT_FALSE(TT.probe(key, e));
}

// --- Birim: derinlik-tercihli değiştirme aynı anahtarı günceller ---
TEST(TT, DeeperReplacesSameKey) {
    TT.clear();
    const std::uint64_t key = 0x5555AAAA5555AAAAULL;
    TT.store(key, 2, 50, Bound::EXACT, Move::make(B1, C3));
    TT.store(key, 6, 80, Bound::EXACT, Move::make(G1, F3));  // daha derin: üstüne yaz
    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.depth, 6);
    EXPECT_EQ(e.score, 80);
}

namespace {

// Aynı FEN'i verilen derinlikte arar. TT'yi çağırandan yönetiyoruz.
SearchResult search_fen(const char* fen, int depth) {
    Board b;
    EXPECT_TRUE(b.set_fen(fen));
    return search(b, depth);
}

const char* kMidgame =
    "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1";

}  // namespace

// --- TT sonucu değiştirmemeli: boş TT vs. doldurulmuş TT aynı skor/hamle ---
TEST(TT, ResultConsistentAcrossTTState) {
    // 1) Temiz TT ile ara.
    TT.clear();
    SearchResult a = search_fen(kMidgame, 5);

    // 2) TT önceki aramadan dolu; tekrar ara -> aynı sonuç gelmeli.
    SearchResult b = search_fen(kMidgame, 5);

    EXPECT_EQ(a.best, b.best);
    EXPECT_EQ(a.score, b.score);

    // 3) TT tamamen temizlenip yeniden arandığında da aynı olmalı (determinizm).
    TT.clear();
    SearchResult c = search_fen(kMidgame, 5);
    EXPECT_EQ(a.best, c.best);
    EXPECT_EQ(a.score, c.score);
}

// --- TT işini yapıyor: dolu TT ile yeniden arama daha az düğüm gezmeli ---
TEST(TT, ReducesNodesOnResearch) {
    TT.clear();
    SearchResult first  = search_fen(kMidgame, 6);
    SearchResult second = search_fen(kMidgame, 6);  // TT şimdi dolu

    EXPECT_LT(second.nodes, first.nodes);
}

// --- Mat skoru TT üzerinden de korunur (ply düzeltmesi doğru) ---
TEST(TT, MateScorePreservedThroughTT) {
    TT.clear();
    // Mat-in-1 pozisyonu; ilk arama TT'yi doldurur, ikincisi TT'den okur.
    const char* mate = "6k1/5ppp/8/8/8/8/8/R6K w - - 0 1";
    SearchResult a = search_fen(mate, 3);
    SearchResult b = search_fen(mate, 3);
    EXPECT_TRUE(is_mate_score(a.score));
    EXPECT_TRUE(is_mate_score(b.score));
    EXPECT_EQ(a.score, b.score);
    EXPECT_EQ(a.best, b.best);
}
