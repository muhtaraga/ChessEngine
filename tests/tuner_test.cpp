// Texel tuner + EvalParams kaydet/yükle birim testleri.
//
// Kritik doğrulama (sapma/drift yok): tuner içsel DOĞRUSAL model üzerinde optimize
// eder; tune sonrası GERÇEK motor eval'iyle ölçülen MSE (texel_mse) hem düşmeli hem
// de modelin bildirdiği mse_end'e yakın olmalı. Uyuşmazlık = özellik çıkarımı
// motorun eval'inden sapmış demektir.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "engine/attacks.hpp"
#include "engine/datagen.hpp"
#include "engine/eval.hpp"
#include "engine/tuner.hpp"

using namespace engine;

namespace {
struct TunerInit {
    TunerInit() { init_sliding_attacks(); }
} tuner_init;

// g_eval'i varsayılana döndür (global durum; testler arası sızmayı önle).
void reset_eval_params() { g_eval = make_default_eval_params(); }

// Küçük etiketli veri seti üret -> geçici dosya -> TexelData.
bool make_small_dataset(TexelData& out, const char* tmp) {
    DatagenConfig dg;
    dg.games = 40; dg.depth = 4; dg.opening_plies = 6; dg.max_game_ply = 120; dg.seed = 99;
    std::ofstream f(tmp);
    if (!f) return false;
    generate_training_data(dg, f);
    f.close();
    return load_texel_data(tmp, out);
}
}  // namespace

// Uçtan uca: gerçek motor MSE'si düşer + model gerçekle uyuşur (drift yok) +
// king safety dondurulmuş kalır.
TEST(Tuner, ReducesActualEngineMseNoDrift) {
    reset_eval_params();
    const char* tmp = "tuner_test_data.tmp";
    TexelData data;
    ASSERT_TRUE(make_small_dataset(data, tmp));
    ASSERT_GT(data.pos.size(), 100u);

    // K kalibrasyonu: makul, pozitif; en iyi K'da MSE, K~0'dan (sigmoid~0.5) düşük.
    double k = find_best_k(data);
    EXPECT_GT(k, 0.0);
    EXPECT_LT(texel_mse(data, k), texel_mse(data, 0.01));

    double before = texel_mse(data, k);

    TuneConfig cfg;
    cfg.k = k; cfg.epochs = 400; cfg.lr = 2.0; cfg.verbose = false;
    TuneResult r = run_texel_tune(data, cfg);

    // Tune sonrası g_eval tune edilmiş; GERÇEK motor eval'iyle MSE.
    double after = texel_mse(data, k);

    EXPECT_LT(after, before);                     // gerçek motor iyileşti
    EXPECT_NEAR(after, r.mse_end, 1e-4);          // model == gerçek (drift yok)

    // King safety DONDURULMUŞ kalmalı (tuner bunlara dokunmaz).
    EvalParams def = make_default_eval_params();
    EXPECT_EQ(g_eval.shield_missing, def.shield_missing);
    for (int i = 0; i < PIECE_TYPE_NB; ++i)
        EXPECT_EQ(g_eval.king_attack_weight[i], def.king_attack_weight[i]);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(g_eval.safety_table[i], def.safety_table[i]);

    std::remove(tmp);
    reset_eval_params();
}

// Parametre dosyası kaydet/yükle: tüm alanlar birebir geri gelir.
TEST(EvalParams, SaveLoadRoundTrip) {
    EvalParams p = make_default_eval_params();
    p.material[KNIGHT] = 999;
    p.pst_mg[PAWN][12] = -7;
    p.pst_eg[QUEEN][40] = 33;
    p.passed_mg[5] = 77;
    p.safety_table[3] = 42;

    const char* tmp = "params_roundtrip.tmp";
    ASSERT_TRUE(save_eval_params(p, tmp));

    EvalParams q = make_default_eval_params();  // farklı başlangıç
    ASSERT_TRUE(load_eval_params(q, tmp));

    std::vector<int*> pp = flat_param_pointers(p);
    std::vector<int*> qp = flat_param_pointers(q);
    ASSERT_EQ(pp.size(), qp.size());
    ASSERT_EQ(pp.size(), flat_param_names().size());
    for (std::size_t i = 0; i < pp.size(); ++i)
        EXPECT_EQ(*pp[i], *qp[i]) << "flat idx " << i << " (" << flat_param_names()[i] << ")";

    std::remove(tmp);
}

// eval_frozen_start doğru yerde: altındakiler tunable, üstündekiler king safety.
TEST(EvalParams, FrozenBoundary) {
    int F = eval_frozen_start();
    const auto& names = flat_param_names();
    ASSERT_LT(static_cast<std::size_t>(F), names.size());
    EXPECT_EQ(names[F], "shield_missing");              // ilk dondurulmuş
    EXPECT_EQ(names[F - 1], "rook_semi_eg");            // son tunable
    EXPECT_EQ(names.back(), "safety_table.99");
}
