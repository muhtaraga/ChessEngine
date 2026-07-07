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
