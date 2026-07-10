// Perft testleri: move generation'ın doğruluk kanıtı.
// Referans değerler Chess Programming Wiki "Perft Results" sayfasından.
//
// Not: Debug derlemesinde copy-make + legality-by-copy yavaştır; bu yüzden
// ctest'te makul derinliklerle sınırlıyız. Daha derin perft için standalone
// `chess perft <depth> [fen]` (Release) kullanılır.

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/perft.hpp"

using namespace engine;

namespace {

std::uint64_t perft_fen(const std::string& fen, int depth) {
    Board b;
    EXPECT_TRUE(b.set_fen(fen));
    return perft(b, depth);
}

const char* kStartpos =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char* kKiwipete =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
const char* kPosition3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
const char* kPosition4 =
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
const char* kPosition5 =
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
const char* kPosition6 =
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";

}  // namespace

// --- Başlangıç pozisyonu ---
TEST(Perft, Startpos) {
    EXPECT_EQ(perft_fen(kStartpos, 1), 20u);
    EXPECT_EQ(perft_fen(kStartpos, 2), 400u);
    EXPECT_EQ(perft_fen(kStartpos, 3), 8902u);
    EXPECT_EQ(perft_fen(kStartpos, 4), 197281u);
    EXPECT_EQ(perft_fen(kStartpos, 5), 4865609u);
}

// --- Kiwipete: rok, en passant, pin'lerin yoğun olduğu pozisyon ---
TEST(Perft, Kiwipete) {
    EXPECT_EQ(perft_fen(kKiwipete, 1), 48u);
    EXPECT_EQ(perft_fen(kKiwipete, 2), 2039u);
    EXPECT_EQ(perft_fen(kKiwipete, 3), 97862u);
    EXPECT_EQ(perft_fen(kKiwipete, 4), 4085603u);
}

// --- Pozisyon 3: en passant ve şah ince durumları ---
TEST(Perft, Position3) {
    EXPECT_EQ(perft_fen(kPosition3, 1), 14u);
    EXPECT_EQ(perft_fen(kPosition3, 2), 191u);
    EXPECT_EQ(perft_fen(kPosition3, 3), 2812u);
    EXPECT_EQ(perft_fen(kPosition3, 4), 43238u);
    EXPECT_EQ(perft_fen(kPosition3, 5), 674624u);
}

// --- Pozisyon 4 (ve ayna): promosyon + rok ---
TEST(Perft, Position4) {
    EXPECT_EQ(perft_fen(kPosition4, 1), 6u);
    EXPECT_EQ(perft_fen(kPosition4, 2), 264u);
    EXPECT_EQ(perft_fen(kPosition4, 3), 9467u);
    EXPECT_EQ(perft_fen(kPosition4, 4), 422333u);
}

// --- Pozisyon 5 ---
TEST(Perft, Position5) {
    EXPECT_EQ(perft_fen(kPosition5, 1), 44u);
    EXPECT_EQ(perft_fen(kPosition5, 2), 1486u);
    EXPECT_EQ(perft_fen(kPosition5, 3), 62379u);
    EXPECT_EQ(perft_fen(kPosition5, 4), 2103487u);
}

// --- Pozisyon 6 ---
TEST(Perft, Position6) {
    EXPECT_EQ(perft_fen(kPosition6, 1), 46u);
    EXPECT_EQ(perft_fen(kPosition6, 2), 2079u);
    EXPECT_EQ(perft_fen(kPosition6, 3), 89890u);
    EXPECT_EQ(perft_fen(kPosition6, 4), 3894594u);
}

// --- Diferansiyel test: pin-aware filtre == kopya-tabanlı referans ---
//
// Perft yalnız hamle SAYISINI karşılaştırır; iki hata birbirini götürebilir
// (bir legal hamle elenip bir illegal hamle eklenirse toplam değişmez). Bu test
// perft ağacında yürüyüp iki üretecin AYNI hamleleri AYNI SIRADA verdiğini
// iddia eder — Aşama 1'in davranış-koruyan olduğunun ispatı. (Magic bitboard'ları
// ray-tracing'e karşı doğrulama deseninin aynısı.)
namespace {

void assert_generators_agree(Board& b, int depth) {
    MoveList fast, ref;
    generate_legal(b, fast);
    generate_legal_reference(b, ref);

    ASSERT_EQ(fast.size(), ref.size()) << "FEN: " << b.to_fen();
    for (int i = 0; i < fast.size(); ++i)
        ASSERT_EQ(fast[i].raw(), ref[i].raw())
            << "FEN: " << b.to_fen() << " hamle #" << i;

    if (depth <= 1)
        return;

    for (Move m : fast) {
        Board next = b;
        next.do_move(m);
        assert_generators_agree(next, depth - 1);
    }
}

// En passant'ın pin makinesine görünmeyen tuzağı: ep tek sıradan İKİ taş kaldırır.
// Aşağıdaki iki pozisyonda ep oynamak yatay kale/vezir ışığında şahı açar.
const char* kEpPinHorizontal = "8/8/8/8/k2Pp2Q/8/8/3K4 b - d3 0 1";
const char* kEpPinRook       = "8/8/8/K2pP2r/8/8/8/6k1 w - d6 0 1";
// Çift çek (Re8 + Nf3): yalnız şah hamlesi legaldir.
const char* kDoubleCheck = "4r2k/8/8/8/8/5n2/8/4K3 w - - 0 1";
// Şah gölgesi: Ke4, Ra4'ten çek yiyor. Kf4 ışın üzerinde kalır -> illegal.
// Şah occupancy'den çıkarılmazsa f4 yanlışlıkla "güvenli" görünür.
const char* kKingShadow = "8/4k3/8/8/r3K3/8/8/8 w - - 0 1";

}  // namespace

TEST(Perft, LegalGeneratorMatchesReference) {
    struct Case { const char* fen; int depth; };
    const Case cases[] = {
        {kStartpos, 4},   {kKiwipete, 3},        {kPosition3, 4},
        {kPosition4, 3},  {kPosition5, 3},       {kPosition6, 3},
        {kEpPinHorizontal, 3}, {kEpPinRook, 3},  {kDoubleCheck, 3},
        {kKingShadow, 3},
    };

    for (const Case& c : cases) {
        Board b;
        ASSERT_TRUE(b.set_fen(c.fen)) << c.fen;
        assert_generators_agree(b, c.depth);
    }
}
