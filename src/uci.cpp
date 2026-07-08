// UCI protokol implementasyonu.

#include "engine/uci.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/search.hpp"
#include "engine/tt.hpp"

namespace engine {

namespace {

// Iterative deepening derinlik tavanı.
constexpr int kMaxDepth = 63;
// Derinlik/zaman verilmediğinde (çıplak "go" ya da "go infinite") varsayılan
// süre bütçesi (ms). Tek iş parçacığı olduğu için gerçek "infinite"/stop
// desteklenmiyor; bunun yerine sınırlı süre aranıp bestmove verilir.
constexpr std::int64_t kDefaultBudgetMs = 3000;

// UCI hamle dizesini (ör. "e2e4", "e7e8q") mevcut pozisyondaki legal bir
// hamleyle eşleştirir. Böylece rok/en passant/promosyon bayrakları doğru atanır.
// Eşleşme yoksa geçersiz Move döner.
Move parse_move(const Board& b, const std::string& token) {
    MoveList ml;
    generate_legal(b, ml);
    for (Move m : ml)
        if (m.to_uci() == token)
            return m;
    return Move();
}

// "position" komutunu işler: startpos ya da fen, ardından opsiyonel moves.
void handle_position(Board& b, std::istringstream& ss) {
    std::string token;
    ss >> token;

    if (token == "startpos") {
        b.set_startpos();
        ss >> token;  // muhtemelen "moves"
    } else if (token == "fen") {
        std::string fen;
        // FEN 6 alandan oluşur; "moves" görene kadar topla.
        while (ss >> token && token != "moves")
            fen += token + ' ';
        b.set_fen(fen);
        // token burada ya "moves" ya da boş.
    }

    if (token == "moves") {
        std::string mv;
        while (ss >> mv) {
            Move m = parse_move(b, mv);
            if (m == Move())  // geçersiz hamle -> dur
                break;
            b.do_move(m);
        }
    }
}

// Puanı UCI formatında yazar: "cp X" veya "mate M".
std::string score_string(int score) {
    std::ostringstream os;
    if (is_mate_score(score)) {
        int mate_ply   = MATE - std::abs(score);
        int mate_moves = (mate_ply + 1) / 2;
        os << "mate " << (score > 0 ? mate_moves : -mate_moves);
    } else {
        os << "cp " << score;
    }
    return os.str();
}

// "go" komutunu işler: limitleri belirle, iterative deepening ile ara,
// her derinlikte info yaz, sonunda bestmove ver.
void handle_go(const Board& b, std::istringstream& ss, std::ostream& out) {
    int          depth    = -1;
    std::int64_t movetime = -1;
    std::int64_t wtime = -1, btime = -1, winc = 0, binc = 0;
    bool         infinite = false;

    // Bu arama için TT nesli ilerlet: önceki hamlelerden kalan girişler
    // değiştirmede önceliğini yitirsin (yaşlandırma).
    TT.new_search();

    std::string token;
    while (ss >> token) {
        if      (token == "depth")    ss >> depth;
        else if (token == "movetime") ss >> movetime;
        else if (token == "wtime")    ss >> wtime;
        else if (token == "btime")    ss >> btime;
        else if (token == "winc")     ss >> winc;
        else if (token == "binc")     ss >> binc;
        else if (token == "infinite") infinite = true;
    }

    // Derinlik ve zaman bütçesini belirle.
    int          max_depth = kMaxDepth;
    std::int64_t budget_ms = -1;  // <0 -> zaman sınırı yok (yalnızca "go depth N")

    if (depth > 0) {
        max_depth = (depth < kMaxDepth) ? depth : kMaxDepth;  // sabit derinlik, süresiz
    } else if (movetime > 0) {
        budget_ms = movetime;
    } else if (wtime > 0 || btime > 0) {
        std::int64_t t   = (b.side_to_move == WHITE) ? wtime : btime;
        std::int64_t inc = (b.side_to_move == WHITE) ? winc : binc;
        if (t < 0) t = 0;
        // Basit bütçe: kalan sürenin ~1/30'u + artışın yarısı. (Faz 2'de gelişecek.)
        budget_ms = t / 30 + inc / 2;
        if (budget_ms < 5) budget_ms = 5;
    } else {
        // Çıplak "go" veya "go infinite": varsayılan süre bütçesi.
        budget_ms = kDefaultBudgetMs;
        (void)infinite;
    }

    using clock = std::chrono::steady_clock;
    auto start = clock::now();
    auto elapsed_ms = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   clock::now() - start).count();
    };

    SearchResult best;
    for (int d = 1; d <= max_depth; ++d) {
        // Bu derinlik için kalan süre. Derinlik 1 daima süresiz koşar ki en az
        // bir legal hamle garanti olsun.
        std::int64_t time_for_depth = -1;
        if (budget_ms >= 0 && d > 1) {
            std::int64_t remaining = budget_ms - elapsed_ms();
            if (remaining <= 0)
                break;  // yeni derinlik başlatacak süre yok
            time_for_depth = remaining;
        }

        SearchResult r = search(b, d, time_for_depth);
        if (r.aborted)
            break;  // süre doldu: yarım derinliği at, önceki 'best' kullanılır

        best = r;

        auto elapsed = elapsed_ms();
        out << "info depth " << d
            << " score " << score_string(r.score)
            << " nodes " << r.nodes
            << " time "  << elapsed
            << " nps "   << (elapsed > 0 ? r.nodes * 1000 / elapsed : 0);
        if (!r.pv.empty()) {
            out << " pv";
            for (Move m : r.pv)
                out << ' ' << m.to_uci();
        }
        out << '\n';
        out.flush();

        if (is_mate_score(r.score))
            break;  // mat bulundu, daha derine gitme
    }

    if (best.best == Move())
        out << "bestmove 0000\n";  // hamle yok (mat/pat)
    else
        out << "bestmove " << best.best.to_uci() << '\n';
    out.flush();
}

}  // namespace

void uci_loop(std::istream& in, std::ostream& out) {
    Board board;
    board.set_startpos();

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            out << "id name ChessEngine\n";
            out << "id author Muhtar Agcabay\n";
            out << "uciok\n";
            out.flush();
        } else if (cmd == "isready") {
            out << "readyok\n";
            out.flush();
        } else if (cmd == "ucinewgame") {
            board.set_startpos();
            TT.clear();  // yeni oyun: önceki oyunun girişlerini at
        } else if (cmd == "position") {
            handle_position(board, ss);
        } else if (cmd == "go") {
            handle_go(board, ss, out);
        } else if (cmd == "quit") {
            break;
        }
        // Bilinmeyen komutlar sessizce yok sayılır (UCI önerisi).
    }
}

}  // namespace engine
