// Değerlendirme testleri.

#include <gtest/gtest.h>

#include "engine/board.hpp"
#include "engine/eval.hpp"

using namespace engine;

// Başlangıç pozisyonu simetriktir -> değerlendirme 0.
TEST(Eval, StartposIsZero) {
    Board b;
    b.set_startpos();
    EXPECT_EQ(evaluate(b), 0);
}

// Fazladan bir beyaz vezir: beyaz sırasında büyük pozitif, siyah sırasında
// büyük negatif olmalı (bakış açısı simetrisi).
TEST(Eval, MaterialAdvantagePerspective) {
    Board wb;
    ASSERT_TRUE(wb.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    // Aynı pozisyon ama siyahta bir kale eksik (a8 boş).
    Board b;
    ASSERT_TRUE(b.set_fen("1nbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));

    // Beyaz sırada: beyaz bir kale önde -> yaklaşık +500.
    EXPECT_GT(evaluate(b), 400);

    // Aynı pozisyon siyah sırada: değerlendirme negatif (siyah geride).
    Board b2 = b;
    b2.side_to_move = BLACK;
    EXPECT_LT(evaluate(b2), -400);
}

// Simetrik ayna pozisyonu: renkler yer değiştirince değerlendirme aynı
// (her iki taraf da kendi sırasında aynı puanı görmeli).
TEST(Eval, MirroredPositionSymmetry) {
    Board w, blk;
    // Beyaz sırada, beyazın atı gelişmiş.
    ASSERT_TRUE(w.set_fen("rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R w KQkq - 0 1"));
    // Ayna: siyah sırada, siyahın atı simetrik gelişmiş.
    ASSERT_TRUE(blk.set_fen("rnbqkb1r/pppppppp/5n2/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"));
    EXPECT_EQ(evaluate(w), evaluate(blk));
}

// Oyun fazı: tam kadro başlangıç dizilimi = MAX_PHASE (orta oyun ucu).
TEST(Eval, PhaseFullBoardIsMax) {
    Board b;
    b.set_startpos();
    EXPECT_EQ(game_phase(b), MAX_PHASE);
}

// Oyun fazı: yalnız iki şah = 0 (oyun sonu ucu).
TEST(Eval, PhaseBareKingsIsZero) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));
    EXPECT_EQ(game_phase(b), 0);
}

// Oyun fazı: bir vezir çifti düşünce faz 4 birim azalır (2 vezir * 4).
TEST(Eval, PhaseDropsWhenQueensLeave) {
    Board full, noqueens;
    full.set_startpos();
    // Aynı dizilim, iki vezir yok (d1/d8 boş).
    ASSERT_TRUE(noqueens.set_fen("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1"));
    EXPECT_EQ(game_phase(full) - game_phase(noqueens), 2 * PhaseWeight[QUEEN]);
}

// Tapered eval: oyun sonunda (faz 0) şah merkezde köşedekinden çok daha iyi.
// Her iki pozisyonda siyah şah e8'de sabit; yalnız beyaz şah yeri değişir.
TEST(Eval, KingCentralizedInEndgame) {
    Board center, corner;
    ASSERT_TRUE(center.set_fen("4k3/8/8/8/4K3/8/8/8 w - - 0 1"));  // beyaz şah e4
    ASSERT_TRUE(corner.set_fen("4k3/8/8/8/8/8/8/K7 w - - 0 1"));   // beyaz şah a1
    ASSERT_EQ(game_phase(center), 0);
    EXPECT_GT(evaluate(center), evaluate(corner));
}
