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

// --- Pawn structure testleri (pawn_structure yardımcısıyla, PST gürültüsü yok) ---

// İzole piyon: beyazın a-piyonunun komşu (b) sütununda dost piyon yok -> izole;
// siyahın a7/b7 piyonları bağlı. Beyaz yapısal olarak geride (mg < 0).
TEST(Eval, PawnIsolatedPenalty) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/pp6/8/8/8/8/P7/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    pawn_structure(b, mg, eg);
    // Yalnızca beyaz a-piyonu izole; başka terim yok -> tam ceza kadar.
    EXPECT_EQ(mg, IsolatedPenaltyMg);
    EXPECT_EQ(eg, IsolatedPenaltyEg);
}

// Çift piyon: beyazın a-sütununda iki piyonu (a2,a4), b2 komşusuyla izole değil;
// siyahın çift/izolesi yok. Tam olarak bir çift-piyon cezası görülmeli.
TEST(Eval, PawnDoubledPenalty) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/pp6/8/8/P7/8/PP6/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    pawn_structure(b, mg, eg);
    EXPECT_EQ(mg, DoubledPenaltyMg);
    EXPECT_EQ(eg, DoubledPenaltyEg);
}

// Geçer piyon: beyazın d5+e5 piyonları (birbirinin komşusu -> izole değil),
// siyahta hiç piyon yok -> ikisi de geçer. Bonus pozitif ve oyun sonunda (EG)
// orta oyundan (MG) belirgin büyük olmalı (tapered).
TEST(Eval, PawnPassedBonus) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/3PP3/8/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    pawn_structure(b, mg, eg);
    // d5 ve e5 -> rank index 4 (5. sıra).
    EXPECT_EQ(mg, 2 * PassedBonusMg[4]);
    EXPECT_EQ(eg, 2 * PassedBonusEg[4]);
    EXPECT_GT(eg, mg);  // geçer piyon oyun sonunda daha değerli
}

// Renk simetrisi: beyaz (a2,a4) ve siyah (a5,a7) aynı yapısal kusurları (çift +
// izole) aynalı taşır -> beyaz−siyah katkı tam sıfır, ve evaluate() de 0.
TEST(Eval, PawnStructureSymmetry) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/p7/8/p7/P7/8/P7/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    pawn_structure(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
    EXPECT_EQ(evaluate(b), 0);
}

// --- Mobility testleri ---

// Aktif taş daha çok mobilite: beyaz atı merkezde (d4, 8 hamle), siyah atı köşede
// (a8, 2 hamle). pawn_structure/PST gürültüsü olmadan mobility yardımcısıyla test.
TEST(Eval, MobilityFavorsActivePieces) {
    Board b;
    ASSERT_TRUE(b.set_fen("n3k3/8/8/8/3N4/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    mobility(b, mg, eg);
    // Beyaz Nd4=8 hamle, siyah Na8=2 hamle -> fark 6 kare × at ağırlığı.
    EXPECT_EQ(mg, 6 * MobilityMg[KNIGHT]);
    EXPECT_EQ(eg, 6 * MobilityEg[KNIGHT]);
    EXPECT_GT(mg, 0);
}

// Renk simetrisi: beyaz Rd1 + siyah Rd8 aynalı -> mobility katkısı 0, evaluate 0.
TEST(Eval, MobilitySymmetry) {
    Board b;
    ASSERT_TRUE(b.set_fen("3rk3/8/8/8/8/8/8/3RK3 w - - 0 1"));
    int mg = 0, eg = 0;
    mobility(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
    EXPECT_EQ(evaluate(b), 0);
}

// --- Bishop pair + rook on open/semi-open file testleri ---

// Fil çifti: beyaz iki fil (c1,f1), siyah tek fil (c8) -> beyaz lehine tam bonus.
TEST(Eval, BishopPairBonus) {
    Board b;
    ASSERT_TRUE(b.set_fen("2b1k3/8/8/8/8/8/8/2B1KB2 w - - 0 1"));
    int mg = 0, eg = 0;
    bishop_pair(b, mg, eg);
    EXPECT_EQ(mg, BishopPairMg);
    EXPECT_EQ(eg, BishopPairEg);
}

// Açık sütun: beyaz Rd1 (d-sütununda piyon yok), siyah Ra8 (a7 dost piyonuyla
// kapalı) -> yalnız beyaz açık-sütun bonusu.
TEST(Eval, RookOpenFileBonus) {
    Board b;
    ASSERT_TRUE(b.set_fen("r3k3/p7/8/8/8/8/8/3RK3 w - - 0 1"));
    int mg = 0, eg = 0;
    rook_on_file(b, mg, eg);
    EXPECT_EQ(mg, RookOpenMg);
    EXPECT_EQ(eg, RookOpenEg);
}

// Yarı-açık sütun: beyaz Rd1, d-sütununda yalnız rakip piyon (d7), dost piyon yok.
TEST(Eval, RookSemiOpenFileBonus) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/3p4/8/8/8/8/8/3RK3 w - - 0 1"));
    int mg = 0, eg = 0;
    rook_on_file(b, mg, eg);
    EXPECT_EQ(mg, RookSemiMg);
    EXPECT_EQ(eg, RookSemiEg);
}

// Renk simetrisi: aynalı fil-çifti + açık-sütun kalesi -> iki yardımcı da 0,
// evaluate() de 0.
TEST(Eval, BishopRookSymmetry) {
    Board b;
    // Beyaz Bc1,Bf1,Rd1 ; siyah Bc8,Bf8,Rd8 (dikey ayna). Tüm sütunlar piyonsuz.
    ASSERT_TRUE(b.set_fen("2brkb2/8/8/8/8/8/8/2BRKB2 w - - 0 1"));
    int bmg = 0, beg = 0, rmg = 0, reg = 0;
    bishop_pair(b, bmg, beg);
    rook_on_file(b, rmg, reg);
    EXPECT_EQ(bmg, 0);
    EXPECT_EQ(beg, 0);
    EXPECT_EQ(rmg, 0);
    EXPECT_EQ(reg, 0);
    EXPECT_EQ(evaluate(b), 0);
}

// --- King safety testleri (king_safety yardımcısıyla, PST/materyal gürültüsü yok) ---

// Piyon kalkanı: beyaz şah g1, kalkan f2+h2 var ama g-sütunu boş (1 eksik sütun);
// siyah şah g8 tam kalkanlı (f7,g7,h7). Ortada saldıran taş yok -> yalnız beyazın
// bir eksik kalkan sütunu cezalanır (mg = -ShieldMissingPenalty), eg = 0.
TEST(Eval, KingSafetyPawnShield) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5ppp/8/8/8/8/5P1P/6K1 w - - 0 1"));
    int mg = 0, eg = 0;
    king_safety(b, mg, eg);
    EXPECT_EQ(mg, -ShieldMissingPenalty);
    EXPECT_EQ(eg, 0);
}

// Şah bölgesi saldırısı: beyaz şah g1 tam kalkanlı (f2,g2,h2), siyah vezir g4
// g-sütunundan şah halkasındaki g2'yi vuruyor (units = vezir ağırlığı × 1 kare);
// siyah şah b8 tam kalkanlı, saldırısız. mg = -SafetyTable[QueenWeight].
TEST(Eval, KingSafetyZoneAttack) {
    Board b;
    ASSERT_TRUE(b.set_fen("1k6/ppp5/8/8/6q1/8/5PPP/6K1 w - - 0 1"));
    int mg = 0, eg = 0;
    king_safety(b, mg, eg);
    // Vezir g4, g2'yi (şah halkasında) vurur; başka bölge karesi yok.
    EXPECT_EQ(mg, -SafetyTable[KingAttackWeight[QUEEN]]);
    EXPECT_LT(mg, 0);
    EXPECT_EQ(eg, 0);
}

// Renk simetrisi: her iki şah da aynı kalkan kusurunu (g-sütunu boş) aynalı taşır,
// saldırı yok -> king_safety katkısı tam sıfır, evaluate() de 0.
TEST(Eval, KingSafetySymmetry) {
    Board b;
    ASSERT_TRUE(b.set_fen("6k1/5p1p/8/8/8/8/5P1P/6K1 w - - 0 1"));
    int mg = 0, eg = 0;
    king_safety(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
    EXPECT_EQ(evaluate(b), 0);
}
