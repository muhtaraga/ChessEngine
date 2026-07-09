// SEE (Static Exchange Evaluation) birim testleri: bilinen taş alışverişi
// pozisyonlarında net materyal sonucunu doğrular.

#include <gtest/gtest.h>

#include "engine/attacks.hpp"
#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/see.hpp"

using namespace engine;

namespace {
// Sliding attack tabloları SEE için gerekli (bishop/rook_attacks). init idempotent.
struct SeeInit {
    SeeInit() { init_sliding_attacks(); }
} see_init;
}  // namespace

// Savunmasız vezire piyonla vurmak: tam vezir kazanılır (see = 900).
TEST(See, WinningCaptureFreePiece) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/4q3/3P4/8/8/8/4K3 w - - 0 1"));
    EXPECT_EQ(see(b, Move::make(D5, E6)), 900);  // Pd5xe6, savunmasız
}

// Piyonla, komşu piyonla savunulan piyona vurmak: eşit trade (see = 0).
// Pd4xe5, ardından siyah d6 piyonu ...dxe5 ile geri alır.
TEST(See, EqualTradePawnDefended) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/3p4/4p3/3P4/8/8/4K3 w - - 0 1"));
    EXPECT_EQ(see(b, Move::make(D4, E5)), 0);
}

// Kaleyle, piyonla savunulan piyona vurmak: kayıplı (see = -400).
// Re1xe5 piyonu alır (+100) ama d6 piyonu kaleyi geri alır (-500).
TEST(See, LosingCaptureRookForPawn) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/3p4/4p3/8/8/8/4RK2 w - - 0 1"));
    EXPECT_EQ(see(b, Move::make(E1, E5)), -400);
}

// X-ray: e5 piyonuna kaleyle vurulur; savunan siyah kale arkasında ikinci siyah
// kale var (e-sütunu istiflenmiş). Beyaz kale + arkada beyaz kale de var.
// Beyaz Re1(alt), Re2? Basit istifleme: beyaz e1+e2 kaleleri, siyah e8+e7 kaleleri,
// ortada e5'te siyah piyon. Rxe5: +100(piyon) -500(kale, e7 alır) +500(kale, e2
// alır) -500(kale, e8 alır) = -400. X-ray zinciri doğru çözülmeli.
TEST(See, XrayStackedRooks) {
    Board b;
    ASSERT_TRUE(b.set_fen("4r1k1/4r3/8/4p3/8/8/4R3/4R1K1 w - - 0 1"));
    EXPECT_EQ(see(b, Move::make(E2, E5)), -400);
}

// En passant: beyaz d5 piyonu, siyah c5'e çift itti (ep hedefi c6). d5xc6 ep.
// c6 savunmasızsa see = 100 (bir piyon kazanılır). Ep piyonunun (c5) occ'tan
// kalkması SEE içinde doğru ele alınmalı.
TEST(See, EnPassantWinsPawn) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/2pP4/8/8/8/4K3 w - c6 0 1"));
    EXPECT_EQ(see(b, Move::make(D5, C6, EN_PASSANT)), 100);
}

// --- Sessiz (yakalama olmayan) hamle genelleştirmesi (check-extension SEE-gate) ---

// Sessiz hamle güvenli boş kareye: hiçbir şey alınmaz, taş saldırıya uğramaz ->
// see = 0. Ra1-a5, a5 boş ve siyah tarafından tehdit edilmiyor.
TEST(See, QuietSafeMoveIsZero) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1"));
    EXPECT_EQ(see(b, Move::make(A1, A5)), 0);
}

// Sessiz hamle, taşı düşman piyonuyla savunulan boş kareye koyar: taş asılır ->
// negatif. Ra1-a6, a6 boş ama b7 piyonu vuruyor; kale savunmasız -> see = -500.
TEST(See, QuietHangingPieceIsNegative) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/1p6/8/8/8/8/8/R3K3 w - - 0 1"));
    EXPECT_EQ(see(b, Move::make(A1, A6)), -500);
}
