// Arama implementasyonu: negamax + alpha-beta.

#include "engine/search.hpp"

#include "engine/eval.hpp"
#include "engine/movegen.hpp"

namespace engine {

namespace {

constexpr int INF = 32000;

// Bir düğümün terminal (mat/pat) olup olmadığını, legal hamle listesi boşsa
// belirler. Mat: sıradaki taraf çekte -> -MATE + ply. Pat: 0.
int terminal_score(const Board& b, int ply) {
    Square ksq = b.king_square(b.side_to_move);
    if (is_square_attacked(b, ksq, ~b.side_to_move))
        return -MATE + ply;  // mat edildik
    return 0;                // pat
}

int negamax(const Board& b, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes) {
    ++nodes;

    MoveList ml;
    generate_legal(b, ml);

    // Terminal (mat/pat) — mat puanının doğru olması için derinlikten bağımsız,
    // ufuk düğümünde bile önce kontrol edilir.
    if (ml.size() == 0)
        return terminal_score(b, ply);

    // 50-hamle beraberliği (mat kontrolünden sonra: mat önceliklidir).
    if (b.halfmove_clock >= 100)
        return 0;

    if (depth == 0)
        return evaluate(b);

    int best = -INF;
    for (Move m : ml) {
        Board next = b;
        next.do_move(m);
        int score = -negamax(next, depth - 1, -beta, -alpha, ply + 1, nodes);
        if (score > best)
            best = score;
        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;  // beta kesme
    }
    return best;
}

}  // namespace

SearchResult search(const Board& b, int depth) {
    SearchResult res;

    MoveList ml;
    generate_legal(b, ml);
    ++res.nodes;

    if (ml.size() == 0) {
        // Kökte terminal: mat ise -MATE, pat ise 0. En iyi hamle yok.
        res.score = terminal_score(b, 0);
        return res;
    }

    int alpha = -INF;
    int best  = -INF;
    Move best_move = ml[0];

    for (Move m : ml) {
        Board next = b;
        next.do_move(m);
        int score = -negamax(next, depth - 1, -INF, -alpha, 1, res.nodes);
        if (score > best) {
            best      = score;
            best_move = m;
        }
        if (best > alpha)
            alpha = best;
    }

    res.best  = best_move;
    res.score = best;
    return res;
}

}  // namespace engine
