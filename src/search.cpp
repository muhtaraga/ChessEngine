// Arama implementasyonu: negamax + alpha-beta + PV çıkarımı + zaman kesmesi.

#include "engine/search.hpp"

#include <chrono>
#include <utility>

#include "engine/eval.hpp"
#include "engine/movegen.hpp"
#include "engine/tt.hpp"

namespace engine {

namespace {

constexpr int INF     = 32000;
constexpr int MAX_PLY = 64;

using Clock = std::chrono::steady_clock;

// Mat skorları TT'de ply'den bağımsız saklanmalı: bir düğümde "N ply sonra mat"
// olan skor, farklı derinlikteki bir düğümde yeniden kullanılabilsin diye
// düğümün ply'sine göre normalize edilir. Saklarken ply ekle, okurken çıkar.
int score_to_tt(int score, int ply) {
    if (score >= MATE_IN_MAX)  return score + ply;
    if (score <= -MATE_IN_MAX) return score - ply;
    return score;
}
int score_from_tt(int score, int ply) {
    if (score >= MATE_IN_MAX)  return score - ply;
    if (score <= -MATE_IN_MAX) return score + ply;
    return score;
}

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

    // 50-hamle beraberliği: TT sondasından ÖNCE. halfmove_clock Zobrist
    // anahtarına dahil değil; TT kesmesi bu beraberliği gözden kaçırabilirdi.
    if (b.halfmove_clock >= 100)
        return 0;

    if (depth == 0 || ply >= MAX_PLY - 1)
        return evaluate(b);

    // --- Transposition table sondası ---
    TTEntry tte;
    const bool tt_hit  = TT.probe(b.key, tte);
    const Move tt_move = tt_hit ? tte.move : Move();

    // Kökte (ply == 0) kesme yapma: kök daima tam aranmalı ki bestmove ve PV
    // güvenilir olsun. Yeterince derin bir girişte sınıra göre kes.
    if (tt_hit && ply > 0 && tte.depth >= depth) {
        int s = score_from_tt(tte.score, ply);
        if (tte.bound == Bound::EXACT)                 return s;
        if (tte.bound == Bound::LOWER && s >= beta)    return s;
        if (tte.bound == Bound::UPPER && s <= alpha)   return s;
    }

    // TT hamlesini öne al: basit ama etkili move ordering. Kapsamlı sıralama
    // (MVV-LVA, killer, history) bir sonraki Faz 2 adımında gelecek.
    if (tt_move != Move()) {
        for (int i = 0; i < ml.count; ++i)
            if (ml.moves[i] == tt_move) {
                std::swap(ml.moves[0], ml.moves[i]);
                break;
            }
    }

    const int alpha_orig = alpha;
    int  best      = -INF;
    Move best_move = Move();

    for (Move m : ml) {
        Board next = b;
        next.do_move(m);
        int score = -negamax(next, depth - 1, -beta, -alpha, ply + 1);

        if (aborted)
            return best;  // süre doldu: yarım sonucu bırak (çağıran yok sayar)

        if (score > best) {
            best      = score;
            best_move = m;
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

    // --- Sonucu TT'ye sakla ---
    // best <= alpha_orig: hiçbir hamle alpha'yı geçmedi -> üst sınır (fail-low).
    // best >= beta      : beta kesmesi -> alt sınır (fail-high).
    // aksi              : gerçek değer (exact).
    Bound bound = (best <= alpha_orig) ? Bound::UPPER
                : (best >= beta)       ? Bound::LOWER
                :                        Bound::EXACT;
    TT.store(b.key, depth, score_to_tt(best, ply), bound, best_move);

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
