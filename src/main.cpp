// chess: debug demo ve perft çalıştırıcı.
//   chess                     -> boş tahta + başlangıç pozisyonunu basar
//   chess perft <depth> [fen] -> perft divide (kök hamle başına düğüm) + toplam

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "engine/attacks.hpp"
#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/perft.hpp"

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

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Windows konsolunda Unicode figürinler için UTF-8 kod sayfası.
    SetConsoleOutputCP(CP_UTF8);
#endif

    engine::init_sliding_attacks();

    if (argc >= 3 && std::string(argv[1]) == "perft")
        return run_perft(argc, argv);

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
