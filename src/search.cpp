// Arama implementasyonu: negamax + alpha-beta + PV çıkarımı + zaman kesmesi.

#include "engine/search.hpp"

#include <chrono>

#include "engine/eval.hpp"
#include "engine/movegen.hpp"

namespace engine {

namespace {

constexpr int INF     = 32000;
constexpr int MAX_PLY = 64;

using Clock = std::chrono::steady_clock;

// Bir düğümün terminal (mat/pat) skoru. Mat: sıradaki taraf çekte -> -MATE+ply.
int terminal_score(const Board& b, int ply) {
    Square ksq = b.king_square(b.side_to_move);
    if (is_square_attacked(b, ksq, ~b.side_to_move))
        return -MATE + ply;  // mat edildik
    return 0;                // pat
}

// Tek iş parçacıklı arama durumu. Triangular PV table + zaman kesmesi.
struct Searcher {
    std::uint64_t     nodes = 0;
    bool              aborted = false;
    bool              use_deadline = false;
    Clock::time_point deadline;

    Move pv_table[MAX_PLY][MAX_PLY];
    int  pv_len[MAX_PLY] = {};

    int negamax(const Board& b, int depth, int alpha, int beta, int ply);
};

int Searcher::negamax(const Board& b, int depth, int alpha, int beta, int ply) {
    // Süre kontrolü (her ~4096 düğümde bir; saat çağrısı pahalı).
    if (use_deadline && (nodes & 4095) == 0 && Clock::now() >= deadline)
        aborted = true;
    if (aborted)
        return 0;

    ++nodes;
    pv_len[ply] = ply;  // bu ply için PV başlangıçta boş

    MoveList ml;
    generate_legal(b, ml);

    if (ml.size() == 0)
        return terminal_score(b, ply);

    if (b.halfmove_clock >= 100)
        return 0;

    if (depth == 0 || ply >= MAX_PLY - 1)
        return evaluate(b);

    int best = -INF;
    for (Move m : ml) {
        Board next = b;
        next.do_move(m);
        int score = -negamax(next, depth - 1, -beta, -alpha, ply + 1);

        if (aborted)
            return best;  // süre doldu: yarım sonucu bırak (çağıran yok sayar)

        if (score > best) {
            best = score;
            pv_table[ply][ply] = m;
            for (int j = ply + 1; j < pv_len[ply + 1]; ++j)
                pv_table[ply][j] = pv_table[ply + 1][j];
            pv_len[ply] = pv_len[ply + 1];
        }

        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;
    }
    return best;
}

}  // namespace

SearchResult search(const Board& b, int depth, std::int64_t max_time_ms) {
    Searcher s;
    if (max_time_ms >= 0) {
        s.use_deadline = true;
        s.deadline = Clock::now() + std::chrono::milliseconds(max_time_ms);
    }

    int score = s.negamax(b, depth, -INF, INF, 0);

    SearchResult res;
    res.nodes   = s.nodes;
    res.aborted = s.aborted;
    if (!s.aborted) {
        res.score = score;
        for (int j = 0; j < s.pv_len[0]; ++j)
            res.pv.push_back(s.pv_table[0][j]);
        res.best = res.pv.empty() ? Move() : res.pv[0];
    }
    return res;
}

}  // namespace engine
