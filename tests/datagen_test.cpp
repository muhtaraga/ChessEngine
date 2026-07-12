// Datagen (Texel/NNUE veri üretimi) birim testleri: üretilen her satırın quiet
// filtre değişmezlerini (invariant) taşıdığını doğrular. Self-play stokastik
// olduğundan tam çıktı değil, YAPI test edilir: her FEN legal yüklenir, pozisyon
// çekte değildir, etiket ∈ {0.0, 0.5, 1.0}.

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "engine/attacks.hpp"
#include "engine/board.hpp"
#include "engine/datagen.hpp"
#include "engine/movegen.hpp"

using namespace engine;

namespace {
struct DatagenInit {
    DatagenInit() { init_sliding_attacks(); }
} datagen_init;

// "<FEN(6 alan)> <result>" satırını FEN ve etikete ayırır.
bool split_line(const std::string& line, std::string& fen, std::string& label) {
    std::istringstream is(line);
    std::string f[6], lbl;
    for (int i = 0; i < 6; ++i)
        if (!(is >> f[i])) return false;
    if (!(is >> lbl)) return false;
    fen = f[0] + ' ' + f[1] + ' ' + f[2] + ' ' + f[3] + ' ' + f[4] + ' ' + f[5];
    label = lbl;
    return true;
}
}  // namespace

// Küçük deterministik koşu: üretilen her satır legal + quiet + geçerli etiket.
TEST(Datagen, EmitsLegalQuietLabeledPositions) {
    DatagenConfig cfg;
    cfg.games        = 20;
    cfg.depth        = 4;   // testin hızlı olması için sığ
    cfg.opening_plies = 6;
    cfg.max_game_ply = 120;
    cfg.seed         = 42;

    std::ostringstream out;
    std::uint64_t n = generate_training_data(cfg, out);

    // Stokastik ama depth-4/20 oyunda pratikte bol quiet pozisyon çıkar.
    EXPECT_GT(n, 0u);

    std::istringstream in(out.str());
    std::string line;
    std::uint64_t counted = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        ++counted;

        std::string fen, label;
        ASSERT_TRUE(split_line(line, fen, label)) << "Bozuk satir: " << line;

        // Etiket beyaz-bakışı oyun sonucu olmalı.
        EXPECT_TRUE(label == "0.0" || label == "0.5" || label == "1.0")
            << "Gecersiz etiket: " << label;

        // FEN legal yüklenmeli (round-trip altyapısı).
        Board b;
        ASSERT_TRUE(b.set_fen(fen)) << "Yuklenemeyen FEN: " << fen;

        // Quiet filtresi: kaydedilen pozisyon çekte OLMAMALI.
        MoveGenContext ctx = make_context(b);
        EXPECT_EQ(ctx.checkers, 0u) << "Cekteki pozisyon sizdi: " << fen;
    }

    EXPECT_EQ(counted, n);  // sayaç ile dönen değer tutarlı
}

// Aynı tohum -> aynı çıktı (tekrar-üretilebilirlik).
TEST(Datagen, DeterministicForFixedSeed) {
    DatagenConfig cfg;
    cfg.games        = 8;
    cfg.depth        = 4;
    cfg.opening_plies = 6;
    cfg.max_game_ply = 120;
    cfg.seed         = 7;

    std::ostringstream a, b;
    generate_training_data(cfg, a);
    generate_training_data(cfg, b);
    EXPECT_EQ(a.str(), b.str());
}
