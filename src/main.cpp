// chess: UCI motoru + yardımcı modlar.
//   chess                     -> UCI komut döngüsü (GUI'ler için varsayılan)
//   chess perft <depth> [fen] -> perft divide (kök hamle başına düğüm) + toplam
//   chess fen [uci-moves...]  -> hamleleri oynayıp sonuç FEN'ini basar
//   chess datagen <games> <out.txt> [seed] [depth] -> Texel/NNUE veri seti üretir
//   chess tune <data.txt> <out-params.txt> [epochs] [lr] -> Texel tuning
//   chess demo                -> boş tahta + başlangıç pozisyonunu basar

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#include "engine/attacks.hpp"
#include "engine/board.hpp"
#include "engine/datagen.hpp"
#include "engine/eval.hpp"
#include "engine/movegen.hpp"
#include "engine/perft.hpp"
#include "engine/tuner.hpp"
#include "engine/uci.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Kök seviyede her legal hamle için alt-düğüm sayısını basar (debug için).
std::uint64_t perft_divide(const engine::Board& b, int depth) {
    using namespace engine;
    MoveList list;
    generate_legal(b, list);

    std::uint64_t total = 0;
    for (Move m : list) {
        Board next = b;
        next.do_move(m);
        std::uint64_t n = (depth <= 1) ? 1 : perft(next, depth - 1);
        total += n;
        std::cout << m.to_uci() << ": " << n << '\n';
    }
    return total;
}

int run_perft(int argc, char** argv) {
    using namespace engine;
    int depth = std::stoi(argv[2]);

    std::string fen;
    if (argc >= 4) {
        fen = argv[3];
        for (int i = 4; i < argc; ++i)
            fen += ' ' + std::string(argv[i]);
    }

    Board b;
    if (fen.empty())
        b.set_startpos();
    else if (!b.set_fen(fen)) {
        std::cerr << "Gecersiz FEN\n";
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();
    std::uint64_t total = perft_divide(b, depth);
    auto t1 = std::chrono::steady_clock::now();

    double sec = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "\nToplam: " << total << '\n';
    std::cout << "Sure: " << sec << " s";
    if (sec > 0)
        std::cout << "  (" << static_cast<std::uint64_t>(total / sec) << " nps)";
    std::cout << '\n';
    return 0;
}

// chess fen [uci-moves...] -> başlangıç pozisyonundan verilen UCI hamlelerini
// oynar ve sonuç FEN'ini basar. Açılış kitabı (SPRT altyapısı) üretimi için:
// hamle dizilerini legal olarak oynayıp doğru FEN (en passant/rok/sayaç dahil)
// üretir; elle FEN yazmanın hata riskini ortadan kaldırır.
int run_fen(int argc, char** argv) {
    using namespace engine;
    Board b;
    b.set_startpos();

    for (int i = 2; i < argc; ++i) {
        std::string uci = argv[i];
        MoveList list;
        generate_legal(b, list);
        Move found;
        for (Move m : list)
            if (m.to_uci() == uci) { found = m; break; }
        if (found == Move()) {
            std::cerr << "Gecersiz/illegal hamle: " << uci << '\n';
            return 1;
        }
        b.do_move(found);
    }

    std::cout << b.to_fen() << '\n';
    return 0;
}

// chess datagen <games> <out.txt> [seed] [depth] -> self-play ile Texel tuning
// (ve ileride NNUE) veri seti üretir. Çıktı: satır başına "<FEN> <result>",
// result oyun sonucu (beyaz bakışı: 0.0/0.5/1.0). Ayrıntı: datagen.hpp.
int run_datagen(int argc, char** argv) {
    using namespace engine;
    if (argc < 4) {
        std::cerr << "Kullanim: chess datagen <games> <out.txt> [seed] [depth]\n";
        return 1;
    }

    DatagenConfig cfg;
    cfg.games = std::stoi(argv[2]);
    std::string out_path = argv[3];
    if (argc >= 5) cfg.seed  = std::stoull(argv[4]);
    if (argc >= 6) cfg.depth = std::stoi(argv[5]);

    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "Cikti dosyasi acilamadi: " << out_path << '\n';
        return 1;
    }

    std::uint64_t n = generate_training_data(cfg, out);
    std::cerr << "Uretilen pozisyon: " << n << " (" << cfg.games << " oyun, derinlik "
              << cfg.depth << ", tohum " << cfg.seed << ")\n";
    return 0;
}

// chess tune <data.txt> <out-params.txt> [epochs] [lr] -> Texel tuning: veri
// setine (FEN + oyun sonucu) karşı eval ağırlıklarını optimize eder, sonucu
// parametre dosyasına yazar. King safety dondurulur. Ayrıntı: tuner.hpp.
int run_tune(int argc, char** argv) {
    using namespace engine;
    if (argc < 4) {
        std::cerr << "Kullanim: chess tune <data.txt> <out-params.txt> [epochs] [lr] [reg]\n";
        return 1;
    }
    std::string data_path = argv[2];
    std::string out_path  = argv[3];

    TuneConfig cfg;
    cfg.verbose = true;
    if (argc >= 5) cfg.epochs = std::stoi(argv[4]);
    if (argc >= 6) cfg.lr     = std::stod(argv[5]);
    if (argc >= 7) cfg.reg    = std::stod(argv[6]);  // varsayılana weight decay (0=yok)

    std::cerr << "Veri yukleniyor: " << data_path << '\n';
    TexelData data;
    if (!load_texel_data(data_path, data)) {
        std::cerr << "Veri yuklenemedi (bos ya da acilamadi): " << data_path << '\n';
        return 1;
    }
    std::cerr << "Pozisyon: " << data.pos.size() << "  ozellik cikariliyor + tune...\n";

    TuneResult r = run_texel_tune(data, cfg);

    if (!save_eval_params(g_eval, out_path)) {
        std::cerr << "Parametre dosyasi yazilamadi: " << out_path << '\n';
        return 1;
    }
    std::cerr << "TAMAM: K=" << r.k << "  MSE " << r.mse_start << " -> " << r.mse_end
              << "  (" << r.epochs << " epoch)  -> " << out_path << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Windows konsolunda Unicode figürinler için UTF-8 kod sayfası.
    SetConsoleOutputCP(CP_UTF8);
#endif

    engine::init_sliding_attacks();

    std::string mode = (argc >= 2) ? argv[1] : "";

    if (mode == "perft" && argc >= 3)
        return run_perft(argc, argv);

    if (mode == "fen")
        return run_fen(argc, argv);

    if (mode == "datagen")
        return run_datagen(argc, argv);

    if (mode == "tune")
        return run_tune(argc, argv);

    if (mode == "demo") {
        engine::Board board;
        board.clear();
        std::cout << "Bos tahta:\n";
        board.print();
        std::cout << '\n';
        board.set_startpos();
        std::cout << "Baslangic pozisyonu:\n";
        board.print();
        return 0;
    }

    // Varsayılan: UCI komut döngüsü.
    engine::uci_loop(std::cin, std::cout);
    return 0;
}
