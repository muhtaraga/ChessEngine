// Arama implementasyonu: negamax + alpha-beta + PV çıkarımı + zaman kesmesi.

#include "engine/search.hpp"

#include <algorithm>
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
    // Asenkron durdurma bayrağı (UCI "stop"). Deadline'dan bağımsız yoklanır ki
    // "go infinite" (zaman sınırsız) aramaları da durdurulabilsin.
    const std::atomic<bool>* stop = nullptr;

    Move pv_table[MAX_PLY][MAX_PLY];
    int  pv_len[MAX_PLY] = {};

    // Killer moves: her ply'de beta kesmesi yapan iki quiet hamle. Aynı derinlik
    // seviyesindeki kardeş düğümlerde erken denenir.
    Move killers[MAX_PLY][2] = {};

    // History heuristic: [renk][from][to] için, geçmişte kesme yapan quiet
    // hamlelerin biriken puanı. Quiet hamle sıralamasını yönlendirir.
    int history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    // Kök (ply 0) izleme: bu derinlikte kökte bulunan en iyi hamle/puan. Süre
    // dolup arama yarıda kesilse bile en son iyileşmeyi elde tutmak için (bkz.
    // search_iterative abort dalı). Her derinlik başında sıfırlanır.
    Move root_best_move  = Move();
    int  root_best_score = -INF;

    // Pozisyon geçmişi (Zobrist anahtarları): tekrar (repetition) tespiti için.
    // Arama başında oyun geçmişiyle (kök hariç atalar) tohumlanır; negamax her
    // düğümde kendi anahtarını push/pop eder. is_repetition ataları tarar.
    std::vector<std::uint64_t> keys;

    // Güncel düğümün (b) daha önce oynanıp oynanmadığı: keys içinde (yalnız
    // atalar; b henüz push edilmemiş) aynı-sıra pariteli (step 2) geriye,
    // halfmove_clock ile sınırlı bir tarama. İlk eşleşme -> beraberlik.
    bool is_repetition(const Board& b) const {
        int n     = static_cast<int>(keys.size());
        int limit = std::min<int>(b.halfmove_clock, n);
        // En kısa tekrar döngüsü 4 ply; aynı tarafın sırası olduğundan 2'şer geri.
        for (int i = 4; i <= limit; i += 2)
            if (keys[n - i] == b.key)
                return true;
        return false;
    }

    int  negamax(const Board& b, int depth, int alpha, int beta, int ply);
    int  quiescence(const Board& b, int alpha, int beta, int ply);
    int  score_move(const Board& b, Move m, Move tt_move, int ply) const;
    void update_quiet_stats(const Board& b, Move m, int ply, int depth);

    // Kökte aspiration window ile arar: bir önceki derinliğin puanı etrafında
    // dar pencere dene, fail-low/high olursa genişleterek yeniden ara.
    int  search_root(const Board& b, int depth, int prev_score);
};

// Düğüm anahtarını arama yığınına ekleyip kapsam çıkışında (her return yolunda,
// abort dahil) çıkaran RAII yardımcısı. negamax döngü ortasında return edebildiği
// için pop'un garanti olması şart.
struct KeyGuard {
    std::vector<std::uint64_t>& keys;
    explicit KeyGuard(std::vector<std::uint64_t>& k, std::uint64_t key) : keys(k) {
        keys.push_back(key);
    }
    ~KeyGuard() { keys.pop_back(); }
    KeyGuard(const KeyGuard&) = delete;
    KeyGuard& operator=(const KeyGuard&) = delete;
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
    // Kesme kontrolü (her ~4096 düğümde bir; saat/atomik okuma nispeten pahalı).
    // Önce dış "stop" bayrağı (zaman sınırından bağımsız), sonra deadline.
    if ((nodes & 4095) == 0) {
        if (stop && stop->load(std::memory_order_relaxed))
            aborted = true;
        else if (use_deadline && Clock::now() >= deadline)
            aborted = true;
    }
    if (aborted)
        return 0;

    ++nodes;
    pv_len[ply] = ply;  // bu ply için PV başlangıçta boş

    MoveList ml;
    generate_legal(b, ml);

    if (ml.size() == 0)
        return terminal_score(b, ply);

    // Tekrar (repetition) beraberliği: kökte (ply 0) uygulanmaz — kök daima bir
    // hamle üretmeli. Arama, tekrarı 0 skorladığından önde olan taraf tekrar
    // hattını reddeder, geride olan (perpetual vb.) onu kurtarıcı olarak arar.
    // 50-hamle ile birlikte TT sondasından ÖNCE: bu beraberlikler Zobrist
    // anahtarında yok, TT kesmesi onları gözden kaçırabilirdi.
    if (ply > 0 && is_repetition(b))
        return 0;

    // 50-hamle beraberliği: TT sondasından ÖNCE. halfmove_clock Zobrist
    // anahtarına dahil değil; TT kesmesi bu beraberliği gözden kaçırabilirdi.
    if (b.halfmove_clock >= 100)
        return 0;

    // Horizon: statik eval yerine sessiz-arama (quiescence) — yakalama zincirleri
    // sessizleşene kadar aranır, böylece "yarım kalmış" taş alışverişleri doğru
    // değerlendirilir (horizon effect azalır). Terminal/50-hamle yukarıda ele
    // alındı; buraya yalnızca sessiz olmayabilecek gerçek yaprak düğümler gelir.
    if (depth == 0 || ply >= MAX_PLY - 1)
        return quiescence(b, alpha, beta, ply);

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

    // Bu düğümün anahtarını arama yığınına ekle (çocuklar tekrarı görebilsin);
    // RAII ile fonksiyon çıkışında (abort dahil her yolda) çıkarılır.
    KeyGuard key_guard(keys, b.key);

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

        // PVS (Principal Variation Search): ilk (en iyi sıralanan) hamle tam
        // pencereyle aranır ve PV adayı kabul edilir. Kalan hamleler önce
        // null-window (scout) [-alpha-1, -alpha] ile aranır — bu yalnızca "bu
        // hamle alpha'dan iyi mi?" sorusunu ucuza (daha çok budamayla) yanıtlar.
        // Scout alpha'yı geçerse (ve beta'nın altındaysa) varsayım yanlıştı: gerçek
        // değeri bulmak için tam pencereyle yeniden aranır. İyi move ordering'de
        // re-search nadirdir, net kazanç düğüm sayısında. (LMR'nin oturacağı çerçeve.)
        int score;
        if (i == 0) {
            score = -negamax(next, depth - 1, -beta, -alpha, ply + 1);
        } else {
            score = -negamax(next, depth - 1, -alpha - 1, -alpha, ply + 1);
            if (!aborted && score > alpha && score < beta)
                score = -negamax(next, depth - 1, -beta, -alpha, ply + 1);
        }

        if (aborted)
            return best;  // süre doldu: yarım sonucu bırak (çağıran yok sayar)

        if (score > best) {
            best      = score;
            best_move = m;
            pv_table[ply][ply] = m;
            for (int j = ply + 1; j < pv_len[ply + 1]; ++j)
                pv_table[ply][j] = pv_table[ply + 1][j];
            pv_len[ply] = pv_len[ply + 1];

            // Kök iyileşmesini anında kaydet: süre dolup arama kesilse bile
            // (aşağıdaki abort dönüşü) bu hamle search_iterative'de kullanılabilir.
            if (ply == 0) {
                root_best_move  = m;
                root_best_score = score;
            }
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

// Quiescence (sessiz-arama): negamax'ın yaprağında çağrılır. Yalnızca "gürültülü"
// hamleleri (yakalamalar + promosyonlar) arar; pozisyon sessizleşince statik eval
// döner. Böylece derinlik sınırının tam ortasındaki taş alışverişleri yanlış
// değerlendirilmez. Çekteyken stand-pat yapılmaz ve tüm legal kaçışlar aranır
// (mat/horizon doğruluğu). İyileştirmeler (SEE ile kayıplı yakalama elemesi,
// delta pruning) sonraya bırakıldı.
int Searcher::quiescence(const Board& b, int alpha, int beta, int ply) {
    if ((nodes & 4095) == 0) {
        if (stop && stop->load(std::memory_order_relaxed))
            aborted = true;
        else if (use_deadline && Clock::now() >= deadline)
            aborted = true;
    }
    if (aborted)
        return 0;

    ++nodes;

    if (ply >= MAX_PLY - 1)
        return evaluate(b);

    const bool in_check =
        is_square_attacked(b, b.king_square(b.side_to_move), ~b.side_to_move);

    MoveList ml;
    generate_legal(b, ml);

    int best;
    if (in_check) {
        // Çekteyiz: stand-pat yasak, tüm kaçışları aramalıyız. Kaçış yoksa mat.
        if (ml.size() == 0)
            return -MATE + ply;
        best = -INF;
    } else {
        // Stand-pat: sıradaki taraf hiçbir şey almadan mevcut skoru "cebe atabilir"
        // (sessiz pozisyonda eval alt sınır kabul edilir). Beta'yı aşıyorsa kes.
        int stand_pat = evaluate(b);
        if (stand_pat >= beta)
            return stand_pat;
        if (stand_pat > alpha)
            alpha = stand_pat;
        best = stand_pat;
    }

    // Aranacak hamleler: çekteyken hepsi; değilse yalnızca yakalama + promosyon.
    MoveList todo;
    for (Move m : ml)
        if (in_check || is_capture(b, m) || m.type() == PROMOTION)
            todo.add(m);

    // MVV-LVA (+ promosyon primi) ile skorla.
    int scores[256];
    for (int i = 0; i < todo.count; ++i) {
        Move m = todo.moves[i];
        PieceType victim    = (m.type() == EN_PASSANT) ? PAWN
                            : is_capture(b, m)         ? b.type_on(m.to())
                            :                            PAWN;
        PieceType aggressor = b.type_on(m.from());
        int s = static_cast<int>(victim) * 16 - static_cast<int>(aggressor);
        if (m.type() == PROMOTION)
            s += 1000 + static_cast<int>(m.promotion_type());
        scores[i] = s;
    }

    for (int i = 0; i < todo.count; ++i) {
        // Lazy selection sort: kalanların en yüksek skorlusunu öne getir.
        int best_j = i;
        for (int j = i + 1; j < todo.count; ++j)
            if (scores[j] > scores[best_j])
                best_j = j;
        if (best_j != i) {
            std::swap(todo.moves[i], todo.moves[best_j]);
            std::swap(scores[i],     scores[best_j]);
        }

        Board next = b;
        next.do_move(todo.moves[i]);
        int score = -quiescence(next, -beta, -alpha, ply + 1);

        if (aborted)
            return best;

        if (score > best)
            best = score;
        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;  // fail-high
    }

    return best;
}

// Kökte aspiration window araması. Sığ derinlikte (veya önceki puan mat ise)
// puan kararsız/uçlarda olduğundan tam pencere kullanılır. Aksi halde önceki
// puanın etrafında dar pencere denenir; fail-low/high olursa ilgili sınır
// genişletilerek yeniden aranır (böylece iyi sıralamada çoğu düğüm dar pencereyle,
// yani daha çok budamayla aranır).
int Searcher::search_root(const Board& b, int depth, int prev_score) {
    if (depth <= 2 || is_mate_score(prev_score))
        return negamax(b, depth, -INF, INF, 0);

    int delta = 25;  // ~1/4 piyon başlangıç penceresi
    int alpha = std::max(prev_score - delta, -INF);
    int beta  = std::min(prev_score + delta,  INF);

    while (true) {
        int score = negamax(b, depth, alpha, beta, 0);
        if (aborted)
            return score;

        if (score <= alpha) {
            // Fail-low: gerçek değer alt sınırın altında. Beta'yı ortala (üst
            // yarıyı at), alt sınırı genişlet ve yeniden ara.
            beta  = (alpha + beta) / 2;
            alpha = std::max(score - delta, -INF);
            delta += delta / 2;
        } else if (score >= beta) {
            // Fail-high: gerçek değer üst sınırın üstünde. Üst sınırı genişlet.
            beta = std::min(score + delta, INF);
            delta += delta / 2;
        } else {
            return score;  // pencere içinde: gerçek (exact) değer
        }
    }
}

}  // namespace

SearchResult search(const Board& b, int depth, std::int64_t max_time_ms,
                    const std::vector<std::uint64_t>& history) {
    Searcher s;
    s.keys = history;  // tekrar tespiti için oyun geçmişiyle tohumla
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

SearchResult search_iterative(const Board& b, const SearchLimits& lim,
                              const InfoCallback& info,
                              const std::vector<std::uint64_t>& history) {
    Searcher s;
    s.keys = history;  // tekrar tespiti için oyun geçmişiyle tohumla
    s.stop = lim.stop;
    if (lim.hard_ms >= 0) {
        s.use_deadline = true;
        s.deadline = Clock::now() + std::chrono::milliseconds(lim.hard_ms);
    }

    const auto start = Clock::now();
    auto elapsed_ms = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   Clock::now() - start).count();
    };

    SearchResult best;      // son TAMAMLANAN derinliğin sonucu
    int          prev_score = 0;

    for (int depth = 1; depth <= lim.max_depth; ++depth) {
        s.root_best_move  = Move();
        s.root_best_score = -INF;

        // Derinlik 1 daima kesintisiz koşsun ki en az bir legal hamle garanti
        // olsun (aksi halde çok kısa bütçede ya da hemen gelen "stop"ta bestmove
        // 0000 dönebilirdik). Hem deadline hem stop bu derinlikte devre dışı.
        const bool               saved_deadline = s.use_deadline;
        const std::atomic<bool>* saved_stop     = s.stop;
        if (depth == 1) {
            s.use_deadline = false;
            s.stop         = nullptr;
        }

        int score = s.search_root(b, depth, prev_score);

        s.use_deadline = saved_deadline;
        s.stop         = saved_stop;

        if (s.aborted) {
            // Süre doldu: bu yarım derinliği normalde atarız (önceki 'best'
            // oynanır). Ama kökte önceki derinliğe göre gerçek bir iyileşme
            // bulunduysa onu korumak blunder'ı önler. PV'yi tek hamleye kısalt
            // (kalan varyant yarım aramadan güvenilir değil).
            if (depth > 1 && s.root_best_move != Move() &&
                s.root_best_score > prev_score) {
                best.best  = s.root_best_move;
                best.score = s.root_best_score;
                best.pv.assign(1, s.root_best_move);
            }
            best.nodes   = s.nodes;
            best.aborted = true;
            break;
        }

        // Derinlik tamamlandı: sonucu güncelle.
        prev_score   = score;
        best.score   = score;
        best.nodes   = s.nodes;
        best.aborted = false;
        best.pv.clear();
        for (int j = 0; j < s.pv_len[0]; ++j)
            best.pv.push_back(s.pv_table[0][j]);
        best.best = best.pv.empty() ? Move() : best.pv[0];

        if (info)
            info(best, depth);

        if (is_mate_score(score))
            break;  // mat bulundu: daha derine gitmenin anlamı yok

        // Soft limit: hedef bütçeyi aştıysak bir sonraki (daha pahalı) derinliğe
        // başlama; elimizdeki en iyi hamleyi oyna.
        std::int64_t soft = (lim.soft_ms >= 0) ? lim.soft_ms : lim.hard_ms;
        if (soft >= 0 && elapsed_ms() >= soft)
            break;
    }

    return best;
}

}  // namespace engine
