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

// --- Geri piyon (backward) testleri (backward_pawns yardımcısıyla, izole) ---

// Beyaz c3: komşusu d4 İLERİDE (geriden destek yok) + durak karesi c4 siyah d5
// piyonunca kontrol -> geri. d4 komşusu c3 geride -> desteklenir, geri değil.
// Siyah d5 izole -> geri değil (komşu yok). Yalnız c3 -> tam bir ceza.
TEST(Eval, BackwardPawnPenalty) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/3p4/3P4/2P5/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    backward_pawns(b, mg, eg);
    EXPECT_EQ(mg, BackwardPenaltyMg);
    EXPECT_EQ(eg, BackwardPenaltyEg);
    EXPECT_LT(eg, 0);
}

// Geriden destek: b2 eklenince c3'ün geride komşusu olur -> desteklenebilir, geri
// değil. (İzole terimiyle karışmasın diye: c3'ün komşusu daima var.)
TEST(Eval, BackwardNotIfSupported) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/3p4/3P4/2P5/1P6/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    backward_pawns(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
}

// Durak karesi güvenli: siyah piyon d6'da (c4'ü vurmaz) -> c3 ilerleyebilir, geri
// değil. Komşu (d4) + izole-değil koşulları hâlâ sağlanıyor -> yalnız durak koşulu eler.
TEST(Eval, BackwardNotIfStopSafe) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/3p4/8/3P4/2P5/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    backward_pawns(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
}

// Renk simetrisi (dikey ayna + renk takası, iki tahta -> anti-vacuity): beyaz c3
// geri -> −ceza; ^56 aynası siyah c6 geri -> +ceza (beyaz bakışıyla). Siyah tahta
// beyazın tam negatifi.
TEST(Eval, BackwardSymmetry) {
    Board white_side;
    ASSERT_TRUE(white_side.set_fen("4k3/8/8/3p4/3P4/2P5/8/4K3 w - - 0 1"));
    int mg1 = 0, eg1 = 0;
    backward_pawns(white_side, mg1, eg1);
    EXPECT_EQ(mg1, BackwardPenaltyMg);
    EXPECT_LT(eg1, 0);

    Board black_side;
    ASSERT_TRUE(black_side.set_fen("4k3/8/2p5/3p4/3P4/8/8/4K3 b - - 0 1"));
    int mg2 = 0, eg2 = 0;
    backward_pawns(black_side, mg2, eg2);
    EXPECT_EQ(mg2, -mg1);
    EXPECT_EQ(eg2, -eg1);
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

// --- Threats / hanging testleri (threats yardımcısıyla, PST/materyal gürültüsü yok) ---

// Piyon tehdidi: beyaz e4 piyonu siyah atı d5'i vuruyor; at c6 piyonuyla savunuluyor
// (-> hanging DEĞİL, yalnız piyon tehdidi). Başka tehdit yok -> tam ThreatByPawn.
TEST(Eval, ThreatByPawnBonus) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/2p5/3n4/4P3/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    threats(b, mg, eg);
    EXPECT_EQ(mg, ThreatByPawnMg);
    EXPECT_EQ(eg, ThreatByPawnEg);
}

// Hanging: beyaz kale a1, siyah atı a4'te vuruyor; at savunmasız -> hanging bonusu.
// (Kale→at "threat by rook" DEĞİL — o yalnız kale→vezir; piyon/minör tehdidi de yok.)
// Sonra a4 atı b5 piyonuyla savunulunca hanging kalkar -> katkı 0.
TEST(Eval, ThreatHangingPiece) {
    Board hang;
    ASSERT_TRUE(hang.set_fen("4k3/8/8/8/n7/8/8/R3K3 w - - 0 1"));
    int mg = 0, eg = 0;
    threats(hang, mg, eg);
    EXPECT_EQ(mg, HangingMg);
    EXPECT_EQ(eg, HangingEg);

    Board defended;
    ASSERT_TRUE(defended.set_fen("4k3/8/8/1p6/n7/8/8/R3K3 w - - 0 1"));
    int mg2 = 0, eg2 = 0;
    threats(defended, mg2, eg2);
    EXPECT_EQ(mg2, 0);
    EXPECT_EQ(eg2, 0);
}

// Minör → majör: beyaz at d4 siyah veziri f5'te vuruyor; vezir e6 piyonuyla savunulu
// (-> hanging DEĞİL). Yalnız minör tehdidi -> tam ThreatByMinor. (At e6 piyonunu da
// vuruyor ama piyon "majör" değil, threat_by_minor'a sayılmaz.)
TEST(Eval, ThreatByMinorOnMajor) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/4p3/5q2/3N4/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    threats(b, mg, eg);
    EXPECT_EQ(mg, ThreatByMinorMg);
    EXPECT_EQ(eg, ThreatByMinorEg);
}

// Renk simetrisi: beyaz Pe4 siyah Nd5'i, siyah Pe5 beyaz Nd4'ü aynalı tehdit eder
// (ikisi de savunmasız -> her iki tarafta piyon-tehdit + hanging aynalı). Katkı tam
// sıfır, evaluate() de 0.
TEST(Eval, ThreatsSymmetry) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/3np3/3NP3/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    threats(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
    EXPECT_EQ(evaluate(b), 0);
}

// --- Geçer piyon şah eskortu testleri (yalnız EG; mg her zaman 0) ---

// Beyaz Pd5 (durak karesi d6), kendi şahı Kc5 yakın (Chebyshev 1), rakip şah Kh1 uzak
// (max(4,5)=5) -> eskort BAŞARILI: eg = w * (5 - 1). mg DAİMA 0 (oyun sonu terimi).
TEST(Eval, PasserKingEscortRewardsCloseOwnKing) {
    Board b;
    ASSERT_TRUE(b.set_fen("8/8/8/2KP4/8/8/8/7k w - - 0 1"));
    int mg = 0, eg = 0;
    passer_king_escort(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, PasserKingEscortEg * (5 - 1));
    EXPECT_GT(eg, 0);
}

// TEK YÖNLÜLÜK TESTİ (tasarımın çekirdeği): rakip şah Kd7 durak karesine yakın (1),
// kendi şahımız Ka1 uzak (max(3,5)=5) -> fark NEGATİF ama CEZA YOK, terim 0.
// Simetrik fark formu burada -16 verirdi; max(0,...) tabanı sıfırda tutar -> terim
// passed[] ile kavga edemez (bkz. eval.hpp tasarım notu + ölçüm).
TEST(Eval, PasserKingEscortNeverPenalizes) {
    Board b;
    ASSERT_TRUE(b.set_fen("8/3k4/8/3P4/8/8/8/K7 w - - 0 1"));
    int mg = 0, eg = 0;
    passer_king_escort(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
}

// Sıra kapısı: Pd3 geçer ve kendi şahı (Kc2, mesafe d4'e 1) rakip şahtan (h8, uzak)
// çok daha yakın -> sıra kapısı OLMASAYDI ateşlerdi; göreli sıra 2 (< 3) olduğu için
// terim hiç girmez -> boş geçmez.
TEST(Eval, PasserKingEscortIgnoresEarlyRanks) {
    Board b;
    ASSERT_TRUE(b.set_fen("7k/8/8/8/8/3P4/2K5/8 w - - 0 1"));
    int mg = 0, eg = 0;
    passer_king_escort(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
}

// Renk simetrisi: beyaz Pd5+Kc6, siyah Pd4+kc3 tam dikey ayna (d5^56==d4, c6^56==c3).
// KRİTİK: bu tahtada terim İKİ TARAFTA DA ATEŞLİYOR (her iki şah da kendi piyonunun
// durak karesine 1, rakibinkine 3 -> ikisi de +8 alır) -> katkı sıfır ama BOŞ GEÇMİYOR.
TEST(Eval, PasserKingEscortSymmetry) {
    Board b;
    ASSERT_TRUE(b.set_fen("8/8/2K5/3P4/3p4/2k5/8/8 w - - 0 1"));
    int mg = 0, eg = 0;
    passer_king_escort(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
    EXPECT_EQ(evaluate(b), 0);

    // Anti-vacuity: aynı tahtada siyah şahı uzaklaştırınca beyaz eskortu AÇIĞA çıkar
    // (yani yukarıdaki sıfır, "hiç ateşlemedi" değil "iki taraf da ateşledi ve iptal
    // oldu" demek).
    Board only_white;
    ASSERT_TRUE(only_white.set_fen("8/8/2K5/3P4/8/8/8/7k w - - 0 1"));
    int mg2 = 0, eg2 = 0;
    passer_king_escort(only_white, mg2, eg2);
    EXPECT_GT(eg2, 0);
}

// --- Kale kendi geçer piyonunun arkasında testleri (yalnız EG; mg her zaman 0) ---

// Beyaz Rd1, beyaz Pd5 (geçer, göreli sıra 5), d2-d4 boş (temiz hat) -> kale piyonun
// arkasında ve ilerleyişi destekliyor -> eg = RookBehindPasserEg. mg DAİMA 0.
TEST(Eval, RookBehindPasserBonus) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/3P4/8/8/8/3RK3 w - - 0 1"));
    int mg = 0, eg = 0;
    rook_behind_passer(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, RookBehindPasserEg);
    EXPECT_GT(eg, 0);
}

// İki eleme: (a) araya taş (Nd3) girince between_bb hattı BLOKLU -> bonus yok; (b) kale
// piyonun ÖNÜNDE (Rd7, daha yüksek sıra) -> "arkada" değil -> bonus yok. Naif "sütunda
// dost kale var" implementasyonu ikisinde de yanlışlıkla verirdi; bu testler ayırır.
TEST(Eval, RookBehindPasserBlockedOrInFront) {
    Board blocked;
    ASSERT_TRUE(blocked.set_fen("4k3/8/8/3P4/8/3N4/8/3RK3 w - - 0 1"));
    int mg = 0, eg = 0;
    rook_behind_passer(blocked, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);

    Board in_front;
    ASSERT_TRUE(in_front.set_fen("4k3/3R4/8/3P4/8/8/8/4K3 w - - 0 1"));
    int mg2 = 0, eg2 = 0;
    rook_behind_passer(in_front, mg2, eg2);
    EXPECT_EQ(mg2, 0);
    EXPECT_EQ(eg2, 0);
}

// Kapsam kararının testi: RAKİP kale (Rd1 siyah) bizim geçer piyonumuzun (Pd5) arkasında.
// Option A yalnız KENDİ kaleyi sayar -> beyazın kalesi yok -> etki YOK. (Rakip-kale
// alt-terimi bilinçle dışarıda: rook_on_file yarı-açık örtüşmesi.)
TEST(Eval, RookBehindPasserIgnoresEnemyRook) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/3P4/8/8/8/3rK3 w - - 0 1"));
    int mg = 0, eg = 0;
    rook_behind_passer(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
}

// Renk simetrisi (dikey ayna + renk takası): beyaz Rb1+Pb5 -> +w; ^56 aynası
// siyah Rb8+pb4 -> −w. Her iki tahta da terimi ATEŞLER (anti-vacuity: eg1 != 0), ve
// siyah tahta beyazın tam negatifini vermeli. (Aynı sütunda dikey ayna kale hattını
// bloklardığından tek tahtada iptal kurgusu vacuous olurdu -> iki ayrı tahta kullanıldı.)
TEST(Eval, RookBehindPasserSymmetry) {
    Board white_side;
    ASSERT_TRUE(white_side.set_fen("4k3/8/8/1P6/8/8/8/1R2K3 w - - 0 1"));
    int mg1 = 0, eg1 = 0;
    rook_behind_passer(white_side, mg1, eg1);
    EXPECT_EQ(mg1, 0);
    EXPECT_EQ(eg1, RookBehindPasserEg);
    EXPECT_GT(eg1, 0);

    Board black_side;
    ASSERT_TRUE(black_side.set_fen("1r2k3/8/8/8/1p6/8/8/4K3 b - - 0 1"));
    int mg2 = 0, eg2 = 0;
    rook_behind_passer(black_side, mg2, eg2);
    EXPECT_EQ(mg2, 0);
    EXPECT_EQ(eg2, -eg1);
}

// --- Kötü fil (bad bishop) ---

// Beyaz fil e4 (AÇIK kare); iki beyaz piyon b3 + g4 aynı renkte (açık) ve BLOKSUZ
// (önlerindeki b4/g5 boş). Taban ceza piyon başına, bloke yok. Ceza filin sahibinin
// (beyaz) skorunu DÜŞÜRÜR -> negatif.
TEST(Eval, BadBishopPenalty) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/8/4B1P1/1P6/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    bad_bishop(b, mg, eg);
    EXPECT_EQ(mg, -2 * BadBishopMg);  // 2 açık-kare piyon, bloke yok
    EXPECT_EQ(eg, -2 * BadBishopEg);
    EXPECT_LT(eg, 0);
}

// Bloke ek cezası: beyaz fil c4 (açık), beyaz piyon e4 (açık, filin rengi) ve önündeki
// e5 SİYAH piyonla dolu -> e4 BLOKELİ. Taban + bloke ek ceza uygulanır.
TEST(Eval, BadBishopBlockedExtra) {
    Board b;
    ASSERT_TRUE(b.set_fen("k7/8/8/4p3/2B1P3/8/8/K7 w - - 0 1"));
    int mg = 0, eg = 0;
    bad_bishop(b, mg, eg);
    EXPECT_EQ(mg, -(BadBishopMg + BadBishopBlockedMg));
    EXPECT_EQ(eg, -(BadBishopEg + BadBishopBlockedEg));
    EXPECT_LT(BadBishopMg, BadBishopMg + BadBishopBlockedMg);  // bloke ceza EK
}

// Filin KARŞI rengindeki dost piyonlar sayılmaz: fil e4 (açık), beyaz piyonlar d4 + b2
// (ikisi de KOYU) -> ceza yok.
TEST(Eval, BadBishopIgnoresOppositeColorPawns) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/8/3PB3/8/1P6/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    bad_bishop(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
}

// Yalnız KENDİ piyonları: fil e4 (açık), fil renginde (açık) piyonlar c6 + g6 ama
// hepsi SİYAH (rakip) -> beyazın piyonu yok -> ceza yok.
TEST(Eval, BadBishopIgnoresEnemyPawns) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/2p3p1/8/4B3/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    bad_bishop(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
}

// Renk simetrisi (dikey ayna + renk takası): beyaz Be4 + Pb3 -> −w. ^56 aynası fil VE
// piyonu BİRLİKTE koyu kareye taşır (aynı-renk ilişkisi korunur) -> siyah be5 + pb6 ->
// +w. İki ayrı tahta (anti-vacuity: eg1 != 0), siyah tahta beyazın tam negatifi.
TEST(Eval, BadBishopSymmetry) {
    Board white_side;
    ASSERT_TRUE(white_side.set_fen("4k3/8/8/8/4B3/1P6/8/4K3 w - - 0 1"));
    int mg1 = 0, eg1 = 0;
    bad_bishop(white_side, mg1, eg1);
    EXPECT_EQ(mg1, -BadBishopMg);
    EXPECT_EQ(eg1, -BadBishopEg);
    EXPECT_LT(eg1, 0);

    Board black_side;
    ASSERT_TRUE(black_side.set_fen("4k3/8/1p6/4b3/8/8/8/4K3 b - - 0 1"));
    int mg2 = 0, eg2 = 0;
    bad_bishop(black_side, mg2, eg2);
    EXPECT_EQ(mg2, -mg1);
    EXPECT_EQ(eg2, -eg1);
}

// --- Outpost testleri (outpost() yardımcısıyla; PST/materyal gürültüsü yok) ---

// Beyaz at d5: c4 piyonu destekliyor, göreli 5. sırada, siyahın d5'in ÖNÜNDEKİ komşu
// sütunlarında (c6/c7, e6/e7) piyonu yok -> kovulamaz, tam bonus.
//
// İKİNCİ TAHTA ANTİ-VACUITY TESTİ, DİŞLİ: siyah piyon e4 d5'in ARKASINDA. Piyon yalnız
// ileri gider -> e4 asla d5'i vuramaz -> outpost BOZULMAMALI. Öne-kısıtını unutan naif
// implementasyon (AdjacentFileMask & enemy_pawns) burada 0 döner; doğrusu tam bonus.
// Bu iki implementasyonu ayıran TEK test.
TEST(Eval, OutpostKnightBonus) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/3N4/2P5/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    outpost(b, mg, eg);
    EXPECT_EQ(mg, OutpostKnightMg);
    EXPECT_EQ(eg, OutpostKnightEg);

    Board behind;  // siyah piyon e4: d5'in arkasında, asla vuramaz
    ASSERT_TRUE(behind.set_fen("4k3/8/8/3N4/2P1p3/8/8/4K3 w - - 0 1"));
    int mg2 = 0, eg2 = 0;
    outpost(behind, mg2, eg2);
    EXPECT_EQ(mg2, OutpostKnightMg);
    EXPECT_EQ(eg2, OutpostKnightEg);
}

// Kovulabilirlik kapısı iki biçimde: (a) e7 piyonu e6'ya ilerleyip d5'i vurabilir;
// (b) c6 piyonu d5'i ZATEN vuruyor. İkisinde de sıra + destek kapıları SAĞLANIYOR
// (c4 piyonu destekliyor, göreli sıra 5) -> yalnız kovulabilirlik eliyor, boş geçmez.
TEST(Eval, OutpostDeniedByEnemyPawn) {
    Board can_advance;
    ASSERT_TRUE(can_advance.set_fen("4k3/4p3/8/3N4/2P5/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    outpost(can_advance, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);

    Board attacks_now;
    ASSERT_TRUE(attacks_now.set_fen("4k3/8/2p5/3N4/2P5/8/8/4K3 w - - 0 1"));
    int mg2 = 0, eg2 = 0;
    outpost(attacks_now, mg2, eg2);
    EXPECT_EQ(mg2, 0);
    EXPECT_EQ(eg2, 0);
}

// Fil AYNI outpost karesinde at'tan farklı (daha küçük) ağırlık alır.
TEST(Eval, OutpostBishopWeight) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/3B4/2P5/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    outpost(b, mg, eg);
    EXPECT_EQ(mg, OutpostBishopMg);
    EXPECT_EQ(eg, OutpostBishopEg);
    EXPECT_LT(OutpostBishopMg, OutpostKnightMg);  // at > fil (tasarım kararı)
}

// Kalan iki kapı ayrı ayrı, her biri diğer ikisi sağlanırken düşer:
// (a) d5 atı desteksiz (piyon yok); (b) d3 atı c2 piyonuyla DESTEKLİ + kovulamaz
// (rakip piyon yok) ama göreli 3. sırada -> sıra kapısı eler.
TEST(Eval, OutpostRequiresSupportAndRank) {
    Board unsupported;
    ASSERT_TRUE(unsupported.set_fen("4k3/8/8/3N4/8/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    outpost(unsupported, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);

    Board too_shallow;
    ASSERT_TRUE(too_shallow.set_fen("4k3/8/8/8/8/3N4/2P5/4K3 w - - 0 1"));
    int mg2 = 0, eg2 = 0;
    outpost(too_shallow, mg2, eg2);
    EXPECT_EQ(mg2, 0);
    EXPECT_EQ(eg2, 0);
}

// Renk simetrisi: beyaz Nd5+Pc4 ile siyah nd4+pc5 tam dikey ayna (d5^56==d4,
// c4^56==c5). İki taraf da outpost alır (rakip piyon aynı sırada -> "önde" değil,
// kovamaz) -> katkı tam sıfır, evaluate() de 0.
TEST(Eval, OutpostSymmetry) {
    Board b;
    ASSERT_TRUE(b.set_fen("4k3/8/8/2pN4/2Pn4/8/8/4K3 w - - 0 1"));
    int mg = 0, eg = 0;
    outpost(b, mg, eg);
    EXPECT_EQ(mg, 0);
    EXPECT_EQ(eg, 0);
    EXPECT_EQ(evaluate(b), 0);
}
