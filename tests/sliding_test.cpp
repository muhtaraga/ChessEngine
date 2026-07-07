// Sliding taş (magic bitboard) saldırı testleri.
// Ana strateji: magic üreteçlerin çıktısı, bariz-doğru yavaş ray-tracing
// referansıyla tüm kareler × çok sayıda rastgele occupancy için birebir eşleşmeli.

#include <random>

#include <gtest/gtest.h>

#include "engine/attacks.hpp"
#include "engine/bitboard.hpp"
#include "engine/types.hpp"

using namespace engine;

namespace {
// Test ortamı: magic tabloları bir kez başlat.
struct SlidingInit {
    SlidingInit() { init_sliding_attacks(); }
};
SlidingInit g_init;
}  // namespace

TEST(SlidingAttacks, EmptyBoardCounts) {
    // Boş tahtada kale a1: a sütunu (a2..a8) + 1. sıra (b1..h1) = 14 kare.
    EXPECT_EQ(popcount(rook_attacks(A1, 0)), 14);
    // Boş tahtada kale d4: 7 dikey + 7 yatay = 14.
    EXPECT_EQ(popcount(rook_attacks(D4, 0)), 14);
    // Boş tahtada fil a1: a1-h8 köşegeni (b2..h8) = 7 kare.
    EXPECT_EQ(popcount(bishop_attacks(A1, 0)), 7);
    // Boş tahtada fil d4: 4 köşegen, toplam 13 kare.
    EXPECT_EQ(popcount(bishop_attacks(D4, 0)), 13);
}

TEST(SlidingAttacks, BlockersStopRay) {
    // Kale a1, a4'te blocker: yukarı a2,a3,a4'te durur (a5..a8 görünmez).
    Bitboard occ = square_bb(A4);
    Bitboard ra  = rook_attacks(A1, occ);
    EXPECT_TRUE(test_bit(ra, A2));
    EXPECT_TRUE(test_bit(ra, A3));
    EXPECT_TRUE(test_bit(ra, A4));   // blocker karesi dahil (yenebilir)
    EXPECT_FALSE(test_bit(ra, A5));  // blocker'ın arkası görünmez
}

TEST(SlidingAttacks, MatchesSlowReferenceRandom) {
    std::mt19937_64 rng(0xC0FFEEu);
    for (int s = 0; s < SQUARE_NB; ++s) {
        Square sq = static_cast<Square>(s);
        for (int i = 0; i < 500; ++i) {
            // Seyrek occupancy (gerçekçi tahta dolulukları).
            Bitboard occ = rng() & rng();
            ASSERT_EQ(rook_attacks(sq, occ), slow_rook_attacks(sq, occ))
                << "rook sq=" << s << " occ=" << occ;
            ASSERT_EQ(bishop_attacks(sq, occ), slow_bishop_attacks(sq, occ))
                << "bishop sq=" << s << " occ=" << occ;
        }
    }
}

TEST(SlidingAttacks, QueenIsRookPlusBishop) {
    std::mt19937_64 rng(42);
    for (int s = 0; s < SQUARE_NB; ++s) {
        Square sq = static_cast<Square>(s);
        Bitboard occ = rng() & rng();
        EXPECT_EQ(queen_attacks(sq, occ),
                  rook_attacks(sq, occ) | bishop_attacks(sq, occ));
    }
}
