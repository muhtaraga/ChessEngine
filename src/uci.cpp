// UCI protokol implementasyonu.

#include "engine/uci.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/search.hpp"

namespace engine {

namespace {

constexpr int kDefaultDepth = 6;

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

// "go" komutunu işler: derinliği belirle, ara, bestmove yaz.
void handle_go(const Board& b, std::istringstream& ss, std::ostream& out) {
    int depth = kDefaultDepth;

    std::string token;
    while (ss >> token) {
        if (token == "depth")
            ss >> depth;
        // movetime/wtime/btime vb. zaman yönetimi Faz 2'de.
    }

    SearchResult r = search(b, depth);

    out << "info depth " << depth
        << " score " << score_string(r.score)
        << " nodes " << r.nodes << '\n';

    if (r.best == Move())
        out << "bestmove 0000\n";  // hamle yok (mat/pat)
    else
        out << "bestmove " << r.best.to_uci() << '\n';
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
