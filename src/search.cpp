// Arama implementasyonu: negamax + alpha-beta + PV çıkarımı + zaman kesmesi.

#include "engine/search.hpp"

#include <chrono>
#include <utility>

#include "engine/bitboard.hpp"
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

// Move ordering skor bantları. Yüksek skor önce aranır. Bantlar çakışmayacak
// şekilde ayrık: TT > promosyon/yakalama > killer > history(quiet).
constexpr int kScoreTT       = 2'000'000;  // TT (hash) hamlesi
constexpr int kScoreCapture  = 1'000'000;  // yakalama tabanı (+ MVV-LVA)
constexpr int kScorePromo    =   900'000;  // promosyon ek primi (yakalamaya eklenir)
constexpr int kScoreKiller1  =   800'000;  // 1. killer (bu ply'de kesme yapan quiet)
constexpr int kScoreKiller2  =   790'000;  // 2. killer
constexpr int kHistoryMax    =   700'000;  // history taban tavanı (killer'ın altında)

// Tek iş parçacıklı arama durumu. Triangular PV table + zaman kesmesi +
// move ordering durumu (killer moves, history heuristic).
struct Searcher {
    std::uint64_t     nodes = 0;
    bool              aborted = false;
    bool              use_deadline = false;
    Clock::time_point deadline;

    Move pv_table[MAX_PLY][MAX_PLY];
    int  pv_len[MAX_PLY] = {};

    // Killer moves: her ply'de beta kesmesi yapan iki quiet hamle. Aynı derinlik
    // seviyesindeki kardeş düğümlerde erken denenir.
    Move killers[MAX_PLY][2] = {};

    // History heuristic: [renk][from][to] için, geçmişte kesme yapan quiet
    // hamlelerin biriken puanı. Quiet hamle sıralamasını yönlendirir.
    int history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    int  negamax(const Board& b, int depth, int alpha, int beta, int ply);
    int  score_move(const Board& b, Move m, Move tt_move, int ply) const;
    void update_quiet_stats(const Board& b, Move m, int ply, int depth);
};

// Bir hamlenin yakalama olup olmadığı (en passant dahil). Hedef karede karşı
// renkten taş varsa ya da hamle en passant ise yakalamadır.
bool is_capture(const Board& b, Move m) {
    return m.type() == EN_PASSANT || test_bit(b.colors[~b.side_to_move], m.to());
}

// Move ordering skoru: yüksek = önce ara. TT hamlesi, MVV-LVA'lı yakalamalar,
// promosyonlar, killer'lar ve history sırasıyla değerlendirilir.
int Searcher::score_move(const Board& b, Move m, Move tt_move, int ply) const {
    if (m == tt_move)
        return kScoreTT;

    const MoveType mt  = m.type();
    const bool     cap = is_capture(b, m);

    if (cap || mt == PROMOTION) {
        // MVV-LVA: değerli kurbanı ucuz saldıranla al (victim önce, aggressor ceza).
        PieceType victim    = (mt == EN_PASSANT) ? PAWN
                            : cap                ? b.type_on(m.to())
                            :                      PAWN;  // düz promosyonda kurban yok
        PieceType aggressor = b.type_on(m.from());
        int s = kScoreCapture + static_cast<int>(victim) * 16 - static_cast<int>(aggressor);
        if (mt == PROMOTION)
            s += kScorePromo + static_cast<int>(m.promotion_type());  // vezir promosyonu en güçlü
        return s;
    }

    // Quiet hamle: önce killer, sonra history.
    if (m == killers[ply][0]) return kScoreKiller1;
    if (m == killers[ply][1]) return kScoreKiller2;
    return history[b.side_to_move][m.from()][m.to()];
}

// Beta kesmesi yapan bir quiet hamleyi killer ve history tablolarına işler.
void Searcher::update_quiet_stats(const Board& b, Move m, int ply, int depth) {
    // Killer: yeni hamleyi öne al (zaten 1. killer değilse).
    if (killers[ply][0] != m) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = m;
    }
    // History: derinlik karesi kadar ödüllendir (derin kesmeler daha değerli).
    int& h = history[b.side_to_move][m.from()][m.to()];
    h += depth * depth;
    if (h > kHistoryMax)
        h = kHistoryMax;  // killer bandının altında kalsın
}

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

    // Tüm hamleleri skorla (TT hamlesi, MVV-LVA, killer, history). Her iterasyonda
    // kalanların en yükseğini öne çekiyoruz (lazy selection sort): iyi sıralamada
    // beta kesmesi çoğunlukla ilk birkaç hamlede olur, kalanı sıralamak boşa gider.
    int scores[256];
    for (int i = 0; i < ml.count; ++i)
        scores[i] = score_move(b, ml.moves[i], tt_move, ply);

    const int alpha_orig = alpha;
    int  best      = -INF;
    Move best_move = Move();

    for (int i = 0; i < ml.count; ++i) {
        // Selection sort adımı: [i..) arasında en yüksek skorlu hamleyi i'ye getir.
        int best_j = i;
        for (int j = i + 1; j < ml.count; ++j)
            if (scores[j] > scores[best_j])
                best_j = j;
        if (best_j != i) {
            std::swap(ml.moves[i], ml.moves[best_j]);
            std::swap(scores[i],   scores[best_j]);
        }

        const Move m = ml.moves[i];
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
        if (alpha >= beta) {
            // Beta kesmesi: kesmeyi yapan quiet hamleyi killer + history'ye işle
            // (yakalamalar MVV-LVA ile sıralanır, history'yi kirletmezler).
            if (!is_capture(b, m) && m.type() != PROMOTION)
                update_quiet_stats(b, m, ply, depth);
            break;
        }
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
