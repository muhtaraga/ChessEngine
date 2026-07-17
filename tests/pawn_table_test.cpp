// Pawn hash table + pawn_key testleri.
//
// İki bağımsız şey doğrulanır: (1) pawn_key'in doğru tasarımı (yalnız piyon
// dizilimine bağlı, piyon-dışı taş/stm/rok/ep'den bağımsız), (2) PawnTable
// API'si + cache açık/kapalı evaluate()'in BİREBİR aynı olması (EXACT memoizasyon).

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/board.hpp"
#include "engine/eval.hpp"
#include "engine/pawn_table.hpp"

using namespace engine;

namespace {

// Aynı piyon dizilimi, farklı piyon-DIŞI taşlar (+ farklı stm/rok) -> aynı pawn_key.
TEST(PawnTable, PawnKeyTransposition) {
    Board a, c;
    // Başlangıç piyonları (a2..h2 beyaz, a7..h7 siyah), tüm taşlar yerinde.
    ASSERT_TRUE(a.set_fen(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    // Aynı piyonlar; atlar/filler gelişmiş, sıra siyahta, rok hakları kısıtlı.
    ASSERT_TRUE(c.set_fen(
        "r1bqk2r/pppppppp/2n2n2/2b5/2B5/2N2N2/PPPPPPPP/R1BQK2R b Kq - 4 5"));
    EXPECT_EQ(a.pawn_key, c.pawn_key);
    // Tam anahtar ise farklı olmalı (piyon-dışı durum değişti) -> pawn_key gerçekten
    // key'in daha zayıf, piyona-özel bir alt-kümesi.
    EXPECT_NE(a.key, c.key);
}

// Bir piyon hamlesi pawn_key'i değiştirmeli.
TEST(PawnTable, PawnKeyDiffersOnPawnMove) {
    Board before, after;
    ASSERT_TRUE(before.set_fen(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    ASSERT_TRUE(after.set_fen(  // 1.e4 sonrası
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"));
    EXPECT_NE(before.pawn_key, after.pawn_key);
}

// compute_pawn_key ep/rok/stm'den bağımsız (yalnız piyon psq).
TEST(PawnTable, PawnKeyIgnoresSideAndEp) {
    Board a, b;
    ASSERT_TRUE(a.set_fen("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 w - - 0 1"));
    ASSERT_TRUE(b.set_fen("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 b - - 0 1"));
    EXPECT_EQ(a.pawn_key, b.pawn_key);
    EXPECT_EQ(a.pawn_key, a.compute_pawn_key());
}

TEST(PawnTable, ProbeStoreRoundtrip) {
    PAWN_TABLE.clear();
    const std::uint64_t k = 0x123456789abcdef0ULL;
    int mg = 0, eg = 0;
    std::uint64_t pw = 0, pb = 0;
    EXPECT_FALSE(PAWN_TABLE.probe(k, mg, eg, pw, pb));  // boş -> miss
    // Geçer piyon kümeleri de round-trip olmalı (lockless-XOR key onları da içerir).
    const std::uint64_t set_w = 0x0000001008000000ULL;
    const std::uint64_t set_b = 0x0000000000240000ULL;
    PAWN_TABLE.store(k, -37, 128, set_w, set_b);
    ASSERT_TRUE(PAWN_TABLE.probe(k, mg, eg, pw, pb));
    EXPECT_EQ(mg, -37);
    EXPECT_EQ(eg, 128);
    EXPECT_EQ(pw, set_w);
    EXPECT_EQ(pb, set_b);
}

TEST(PawnTable, ClearEmpties) {
    const std::uint64_t k = 0xdeadbeefcafef00dULL;
    PAWN_TABLE.store(k, 10, -20, 0xffULL, 0xff00ULL);
    int mg = 0, eg = 0;
    std::uint64_t pw = 0, pb = 0;
    ASSERT_TRUE(PAWN_TABLE.probe(k, mg, eg, pw, pb));
    PAWN_TABLE.clear();
    EXPECT_FALSE(PAWN_TABLE.probe(k, mg, eg, pw, pb));
}

// Cache'ten dönen geçer piyon kümesi, ham hesapla BİREBİR aynı olmalı. (Blokaj terimi
// bu kümeye güvenir: küme bozuksa blokaj sessizce yanlış pozisyonlarda ateşler ve
// evaluate() farkı küçük olduğundan gözden kaçabilir.)
TEST(PawnTable, CachedPassedSetsMatchRaw) {
    const std::vector<std::string> fens = {
        "8/2p2pk1/1p1n2p1/p2P3p/P1P4P/1P3NP1/5PK1/8 w - - 0 35",
        "6k1/5ppp/8/8/8/8/1P6/6K1 w - - 0 1",
        "4k3/8/8/3PP3/8/8/8/4K3 w - - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    };
    PAWN_TABLE.clear();
    for (const auto& fen : fens) {
        Board b;
        ASSERT_TRUE(b.set_fen(fen));

        int      raw_mg = 0, raw_eg = 0;
        Bitboard raw_w = 0, raw_b = 0;
        pawn_structure_full(b, raw_mg, raw_eg, raw_w, raw_b);

        PAWN_TABLE.store(b.pawn_key, raw_mg, raw_eg, raw_w, raw_b);
        int           mg = 0, eg = 0;
        std::uint64_t pw = 0, pb = 0;
        ASSERT_TRUE(PAWN_TABLE.probe(b.pawn_key, mg, eg, pw, pb)) << fen;
        EXPECT_EQ(pw, raw_w) << fen;
        EXPECT_EQ(pb, raw_b) << fen;
    }
    PAWN_TABLE.clear();
}

// EXACT: cache açık evaluate() == cache kapalı evaluate() (saf fonksiyon memoizasyonu).
TEST(PawnTable, CachedEqualsRaw) {
    const std::vector<std::string> fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "4k3/8/8/8/8/8/PPP4P/4K3 w - - 0 1",   // izole/çift piyonlar
        "8/pp4pp/8/8/8/8/1P4P1/8 w - - 0 1",   // yalnız piyon (faz=0)
        "6k1/5ppp/8/8/8/8/1P6/6K1 w - - 0 1",  // beyaz geçer piyon
    };

    const bool saved = g_pawn_cache_enabled;
    for (const auto& fen : fens) {
        Board b;
        ASSERT_TRUE(b.set_fen(fen));

        g_pawn_cache_enabled = false;
        int raw = evaluate(b);

        g_pawn_cache_enabled = true;
        PAWN_TABLE.clear();
        int cached_miss = evaluate(b);  // ilk çağrı: miss -> store
        int cached_hit = evaluate(b);   // ikinci çağrı: hit

        EXPECT_EQ(raw, cached_miss) << fen;
        EXPECT_EQ(raw, cached_hit) << fen;
    }
    g_pawn_cache_enabled = saved;
    PAWN_TABLE.clear();
}

// Pawn hash cache YALNIZ piyon-saf terimleri saklayabilir: pawn_key yalnız PİYON
// dizilimini içerir -> aynı piyonlara sahip iki FARKLI pozisyon AYNI girişi paylaşır.
// Bu test, piyon-DIŞI duruma bağlı bir terimin (blockade / şah mesafesi /
// rook-behind-passer gibi — bkz. CLAUDE.md Blok E3 tuzağı) pawn_structure'a sızmasını
// yakalar: sızarsa ikinci pozisyon birincinin BAYAT piyon skorunu alır ve cache'li
// sonuç ham sonuçtan sapar.
// CachedEqualsRaw bunu YAKALAYAMAZ — orada her FEN'den önce cache temizleniyor, yani
// giriş paylaşımı hiç test edilmiyor.
TEST(PawnTable, SamePawnsDifferentPiecesNotStale) {
    // Aynı piyonlar (beyaz c4 + d5), farklı at karesi -> aynı pawn_key, FARKLI eval.
    // At kareleri KÖŞE vs MERKEZ seçildi: at PST'si sol-sağ simetrik olduğundan
    // b6/g6 gibi aynalı kareler BİREBİR aynı değerlenir ve test boş geçerdi.
    Board a, c;
    ASSERT_TRUE(a.set_fen("4k2n/8/8/3P4/2P5/8/8/4K3 w - - 0 1"));   // siyah at h8 (köşe)
    ASSERT_TRUE(c.set_fen("4k3/8/8/3P4/2Pn4/8/8/4K3 w - - 0 1"));   // siyah at d4 (merkez)
    ASSERT_EQ(a.pawn_key, c.pawn_key);  // ön koşul: giriş gerçekten paylaşılıyor

    const bool saved = g_pawn_cache_enabled;

    g_pawn_cache_enabled = false;
    const int raw_a = evaluate(a);
    const int raw_c = evaluate(c);
    // Test boş geçmesin: iki pozisyon gerçekten farklı değerlenmeli (at PST + mobility).
    ASSERT_NE(raw_a, raw_c);

    // Cache açık ve ARADA TEMİZLİK YOK: a'nın sakladığı girişi c de kullanır.
    g_pawn_cache_enabled = true;
    PAWN_TABLE.clear();
    EXPECT_EQ(evaluate(a), raw_a);
    EXPECT_EQ(evaluate(c), raw_c);  // bayat piyon skoru alırsa burada patlar

    g_pawn_cache_enabled = saved;
    PAWN_TABLE.clear();
}

}  // namespace
