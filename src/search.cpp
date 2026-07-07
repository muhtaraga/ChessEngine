// Arama implementasyonu: negamax + alpha-beta + baş varyant (PV) çıkarımı.

#include "engine/search.hpp"

#include "engine/eval.hpp"
#include "engine/movegen.hpp"

namespace engine {

namespace {

constexpr int INF     = 32000;
constexpr int MAX_PLY = 64;

// Bir düğümün terminal (mat/pat) skoru. Mat: sıradaki taraf çekte -> -MATE+ply.
int terminal_score(const Board& b, int ply) {
    Square ksq = b.king_square(b.side_to_move);
    if (is_square_attacked(b, ksq, ~b.side_to_move))
        return -MATE + ply;  // mat edildik
    return 0;                // pat
}

// Tek iş parçacıklı arama durumu. Triangular PV table ile baş varyantı toplar.
struct Searcher {
    std::uint64_t nodes = 0;
    Move pv_table[MAX_PLY][MAX_PLY];
    int  pv_len[MAX_PLY] = {};

    int negamax(const Board& b, int depth, int alpha, int beta, int ply);
};

int Searcher::negamax(const Board& b, int depth, int alpha, int beta, int ply) {
    ++nodes;
    pv_len[ply] = ply;  // bu ply için PV başlangıçta boş

    MoveList ml;
    generate_legal(b, ml);

    // Terminal (mat/pat) — mat puanının doğruluğu için ufuk düğümünde bile önce.
    if (ml.size() == 0)
        return terminal_score(b, ply);

    // 50-hamle beraberliği (mattan sonra: mat önceliklidir).
    if (b.halfmove_clock >= 100)
        return 0;

    if (depth == 0 || ply >= MAX_PLY - 1)
        return evaluate(b);

    int best = -INF;
    for (Move m : ml) {
        Board next = b;
        next.do_move(m);
        int score = -negamax(next, depth - 1, -beta, -alpha, ply + 1);

        if (score > best) {
            best = score;
            // PV güncelle: m + çocuğun PV'si.
            pv_table[ply][ply] = m;
            for (int j = ply + 1; j < pv_len[ply + 1]; ++j)
                pv_table[ply][j] = pv_table[ply + 1][j];
            pv_len[ply] = pv_len[ply + 1];
        }

        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;  // beta kesme
    }
    return best;
}

}  // namespace

SearchResult search(const Board& b, int depth) {
    Searcher s;

    SearchResult res;
    res.score = s.negamax(b, depth, -INF, INF, 0);
    res.nodes = s.nodes;

    for (int j = 0; j < s.pv_len[0]; ++j)
        res.pv.push_back(s.pv_table[0][j]);
    res.best = res.pv.empty() ? Move() : res.pv[0];

    return res;
}

}  // namespace engine
