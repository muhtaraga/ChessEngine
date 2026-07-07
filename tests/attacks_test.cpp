// Bit yardımcıları ve non-sliding saldırı tablosu testleri.

#include <gtest/gtest.h>

#include "engine/attacks.hpp"
#include "engine/bitboard.hpp"
#include "engine/types.hpp"

using namespace engine;

// --- Bit yardımcıları ---

TEST(BitUtils, PopcountAndLsb) {
    EXPECT_EQ(popcount(Bitboard{0}), 0);
    EXPECT_EQ(popcount(square_bb(E4)), 1);
    EXPECT_EQ(popcount(square_bb(A1) | square_bb(H8)), 2);

    EXPECT_EQ(lsb(square_bb(E4)), E4);
    EXPECT_EQ(lsb(square_bb(A1) | square_bb(H8)), A1);
}

TEST(BitUtils, PopLsbIteratesAscending) {
    Bitboard bb = square_bb(C2) | square_bb(A1) | square_bb(H8);
    // pop_lsb en düşük kareden başlayarak sırayla çıkarmalı.
    EXPECT_EQ(pop_lsb(bb), A1);   // index 0
    EXPECT_EQ(pop_lsb(bb), C2);   // index 10
    EXPECT_EQ(pop_lsb(bb), H8);   // index 63
    EXPECT_EQ(bb, 0u);
}

// --- At saldırıları ---

TEST(KnightAttacks, CornerAndCenter) {
    // Köşe: a1 -> b3, c2
    EXPECT_EQ(knight_attacks(A1), square_bb(B3) | square_bb(C2));
    EXPECT_EQ(popcount(knight_attacks(A1)), 2);

    // Köşe: h8 -> f7, g6
    EXPECT_EQ(knight_attacks(H8), square_bb(F7) | square_bb(G6));

    // Merkez: e4 -> 8 hedef
    EXPECT_EQ(popcount(knight_attacks(E4)), 8);
    Bitboard e4 = square_bb(D2) | square_bb(F2) | square_bb(C3) | square_bb(G3) |
                  square_bb(C5) | square_bb(G5) | square_bb(D6) | square_bb(F6);
    EXPECT_EQ(knight_attacks(E4), e4);
}

// --- Şah saldırıları ---

TEST(KingAttacks, CornerEdgeCenter) {
    EXPECT_EQ(king_attacks(A1), square_bb(A2) | square_bb(B1) | square_bb(B2));
    EXPECT_EQ(popcount(king_attacks(A1)), 3);
    EXPECT_EQ(popcount(king_attacks(H1)), 3);
    EXPECT_EQ(popcount(king_attacks(E4)), 8);
}

// --- Piyon saldırıları ---

TEST(PawnAttacks, WhiteAndBlack) {
    // Beyaz e4 -> d5, f5 (yukarı çapraz)
    EXPECT_EQ(pawn_attacks(WHITE, E4), square_bb(D5) | square_bb(F5));
    // Beyaz kenar a2 -> sadece b3
    EXPECT_EQ(pawn_attacks(WHITE, A2), square_bb(B3));
    EXPECT_EQ(pawn_attacks(WHITE, H2), square_bb(G3));

    // Siyah e5 -> d4, f4 (aşağı çapraz)
    EXPECT_EQ(pawn_attacks(BLACK, E5), square_bb(D4) | square_bb(F4));
    EXPECT_EQ(pawn_attacks(BLACK, A7), square_bb(B6));
}
