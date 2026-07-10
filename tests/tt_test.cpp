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

// --- Birim: giriş boyutu (Faz 2D lockless-XOR geometrisinin ön koşulu) ---
TEST(TT, EntryIsSixteenBytes) {
    EXPECT_EQ(sizeof(TTEntry), 16u);
}

// --- Birim: store + probe roundtrip (paketlenmiş alanlar dahil) ---
TEST(TT, StoreProbeRoundtrip) {
    TT.clear();
    const std::uint64_t key = 0xDEADBEEFCAFEBABEULL;
    Move mv = Move::make(E2, E4);
    TT.store(key, /*depth=*/7, /*score=*/123, Bound::EXACT, mv, /*eval=*/-45);

    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.key, key);
    EXPECT_EQ(e.depth, 7);
    EXPECT_EQ(e.score, 123);
    EXPECT_EQ(e.bound(), Bound::EXACT);
    EXPECT_EQ(e.move, mv);
    EXPECT_EQ(e.eval, -45);
    EXPECT_EQ(e.generation(), 0u);
}

// --- Birim: bound ve nesil aynı bayta paketlendi, birbirini bozmuyor ---
TEST(TT, GenerationAndBoundCoexist) {
    TT.clear();
    const std::uint64_t key = 0x0F0F0F0F0F0F0F0FULL;
    for (int i = 0; i < 5; ++i)
        TT.new_search();  // nesil 5
    TT.store(key, 3, 10, Bound::UPPER, Move::make(A2, A3));

    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.bound(), Bound::UPPER);
    EXPECT_EQ(e.generation(), 5u);
}

// --- Birim: nesil sayacı 6 bitte sarar (girişte 6 bit var; maske sayaçta) ---
TEST(TT, GenerationWrapsAtSixtyFour) {
    TT.clear();
    for (int i = 0; i < 64; ++i)
        TT.new_search();
    // 64 arama sonra sayaç yine 0: giriş 0 nesliyle saklanır, 6 bite sığar.
    const std::uint64_t key = 0x1234567890ABCDEFULL;
    TT.store(key, 1, 1, Bound::EXACT, Move());
    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.generation(), 0u);
}

// --- Birim: depth int8'e sığdırılır (UCI "go depth 200" sarmasın) ---
TEST(TT, DepthClampedToMax) {
    TT.clear();
    const std::uint64_t key = 0x00FF00FF00FF00FFULL;
    TT.store(key, /*depth=*/200, 5, Bound::EXACT, Move());
    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.depth, 127);
}

// --- Birim: eval yokluğu (çekteki düğüm) aynı pozisyonun eski eval'ini silmez ---
TEST(TT, PreservesOldEvalWhenNoneStored) {
    TT.clear();
    const std::uint64_t key = 0xAAAA5555AAAA5555ULL;
    TT.store(key, 4, 20, Bound::EXACT, Move::make(D2, D4), /*eval=*/77);
    TT.store(key, 5, 30, Bound::LOWER, Move::make(D2, D4), /*eval=*/kEvalNone);

    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.depth, 5);
    EXPECT_EQ(e.eval, 77);
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

// --- Birim: aynı pozisyonun sığ, exact-olmayan girişi derin girişi EZMEZ ---
// (qsearch girişleri depth 0 ile saklanır; derin negamax bilgisini korumalı.)
TEST(TT, ShallowSameKeyDoesNotReplaceDeeper) {
    TT.clear();
    const std::uint64_t key = 0x1122334455667788ULL;
    TT.store(key, /*depth=*/8, /*score=*/60, Bound::LOWER, Move::make(G1, F3));
    TT.store(key, /*depth=*/0, /*score=*/-5, Bound::UPPER, Move::make(B1, C3));

    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.depth, 8);
    EXPECT_EQ(e.score, 60);
    EXPECT_EQ(e.bound(), Bound::LOWER);
}

// --- Birim: aynı pozisyonun EXACT değeri sığ olsa da yazılır (en iyi bilgi) ---
TEST(TT, ExactReplacesDeeperSameKey) {
    TT.clear();
    const std::uint64_t key = 0x8877665544332211ULL;
    TT.store(key, /*depth=*/8, /*score=*/60, Bound::LOWER, Move::make(G1, F3));
    TT.store(key, /*depth=*/2, /*score=*/17, Bound::EXACT, Move::make(B1, C3));

    TTEntry e;
    ASSERT_TRUE(TT.probe(key, e));
    EXPECT_EQ(e.depth, 2);
    EXPECT_EQ(e.score, 17);
    EXPECT_EQ(e.bound(), Bound::EXACT);
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
