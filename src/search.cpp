// Arama implementasyonu: negamax + alpha-beta + PV çıkarımı + zaman kesmesi.

#include "engine/search.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "engine/bitboard.hpp"
#include "engine/eval.hpp"
#include "engine/movegen.hpp"
#include "engine/see.hpp"
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

// Move ordering skor bantları. Yüksek skor önce aranır. Bantlar çakışmayacak
// şekilde ayrık: TT > promosyon/yakalama > killer > history(quiet).
constexpr int kScoreTT       = 2'000'000;  // TT (hash) hamlesi
constexpr int kScoreCapture  = 1'000'000;  // yakalama tabanı (+ MVV-LVA)
constexpr int kScorePromo    =   900'000;  // promosyon ek primi (yakalamaya eklenir)
constexpr int kScoreKiller1  =   800'000;  // 1. killer (bu ply'de kesme yapan quiet)
constexpr int kScoreKiller2  =   790'000;  // 2. killer
constexpr int kHistoryMax    =   700'000;  // history taban tavanı (killer'ın altında)
constexpr int kScoreBadCapture = -1'000'000;  // kayıplı yakalama (see<0): quiet bandının altında

// History ödül/ceza ölçek çarpanı. Ölçüldü: ölçeksiz (bonus = depth²) tipik |history|
// değerleri 3-100 bandındaydı, tavanın (700k) ~5000 katı altında. Bu iki soruna yol
// açıyordu: age()'deki h/=2 tamsayı bölmesi sinyali siliyordu, ve history-tabanlı
// LMR'nin böleni granülarite bulamıyordu. Çarpan sıralamayı değiştirmez (göreli sıra
// aynı), tavana da çarpmaz (ölçekli maks ~10k << 700k).
constexpr int kHistoryBonusScale = 64;

// Continuation history indekslemesi: (renk, tür) çifti tek bir 0..11 taş indeksi.
// kNoPiece = "bu ply'de gerçek hamle yok" (kök öncesi ya da null move).
constexpr int kNoPiece = 12;
constexpr int piece_index(Color c, PieceType pt) {
    return static_cast<int>(c) * 6 + static_cast<int>(pt);
}

// Bir history girdisini ödüllendirir/cezalandırır ve [-kHistoryMax, +kHistoryMax]
// bandına kırpar. Main history ve continuation history aynı şemayı paylaşır.
inline void add_history(int& h, int bonus) {
    h += bonus;
    if (h >  kHistoryMax) h =  kHistoryMax;
    if (h < -kHistoryMax) h = -kHistoryMax;
}

}  // namespace

// Move ordering tabloları. Sahiplik çağırandadır (bkz. search.hpp SearchTables):
// bir oyun boyunca yaşarlar, her arama başında yaşlanırlar, ucinewgame'de temizlenir.
struct SearchTables::Impl {
    // Killer moves: her ply'de beta kesmesi yapan iki quiet hamle. Aynı derinlik
    // seviyesindeki kardeş düğümlerde erken denenir.
    Move killers[MAX_PLY][2] = {};

    // History heuristic: [renk][from][to] için, geçmişte kesme yapan quiet
    // hamlelerin biriken (işaretli) puanı. Quiet hamle sıralamasını yönlendirir.
    int history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    // Continuation history: [önceki hamlenin taşı][önceki hamlenin hedefi]
    // [bu hamlenin taşı][bu hamlenin hedefi]. Yani "rakip şu taşı şuraya oynadıysa,
    // buna karşı hangi cevap işe yaradı" bilgisi. Main history'den bağımsız bir
    // sinyal; countermove'un taşıdığı bilgiyi sert bant yerine yumuşak puan verir.
    // ~2.36 MB -> heap (Impl unique_ptr arkasında olsa da tablo büyük).
    using ContHist =
        std::array<std::array<std::array<std::array<int, SQUARE_NB>, 12>, SQUARE_NB>, 12>;
    std::unique_ptr<ContHist> cont_hist = std::make_unique<ContHist>();

    // Yeni oyun: her şey sıfır.
    void clear() {
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        cont_hist->fill({});
    }

    // Yeni arama: history yarılanır, killer'lar temizlenir.
    //  - Yarılama iki iş yapar: (a) sert clamp'li biriktirme oyun boyunca ±kHistoryMax
    //    tavanına oturup sinyali doyurmasın (doymuş history = her yerde aynı puan =
    //    bilgi yok), (b) eski pozisyonlardan kalan bilgi zamanla sönsün.
    //  - Killer'lar ply-indeksli: kök iki ply kaydığından bir sonraki aramada aynı
    //    ply artık başka bir pozisyona denk gelir, taşımak anlamsız.
    void age() {
        std::memset(killers, 0, sizeof(killers));
        for (auto& c : history)
            for (auto& f : c)
                for (int& h : f) h /= 2;
        for (auto& a : *cont_hist)
            for (auto& b : a)
                for (auto& c : b)
                    for (int& h : c) h /= 2;
    }
};

SearchTables::SearchTables() : impl(std::make_unique<Impl>()) {}
SearchTables::~SearchTables() = default;
void SearchTables::clear() { impl->clear(); }

namespace {

// Tek iş parçacıklı arama durumu. Triangular PV table + zaman kesmesi + arama
// yığını. Move ordering tabloları dışarıda yaşar (tb), aramalar arası korunur.
struct Searcher {
    SearchTables::Impl& tb;
    explicit Searcher(SearchTables::Impl& t) : tb(t) {}

    std::uint64_t     nodes = 0;
    int               seldepth = 0;  // ulaşılan en derin ply (qsearch/extension dahil)
    bool              aborted = false;
    bool              use_deadline = false;
    Clock::time_point deadline;
    // Asenkron durdurma bayrağı (UCI "stop"). Deadline'dan bağımsız yoklanır ki
    // "go infinite" (zaman sınırsız) aramaları da durdurulabilsin.
    const std::atomic<bool>* stop = nullptr;

    Move pv_table[MAX_PLY][MAX_PLY];
    int  pv_len[MAX_PLY] = {};

    // Arama yığını: stack[p] = ply p'de OYNANAN hamlenin bağlamı (p+1'e götüren
    // hamle). piece == kNoPiece -> o ply'de gerçek hamle yok (null move ya da
    // hiç girilmemiş). Bir düğümde "önceki hamle" = stack[ply-1] (ply >= 1 ise).
    struct StackEntry {
        int    piece = kNoPiece;
        Square to    = SQ_NONE;
    };
    StackEntry stack[MAX_PLY];

    // Bu düğümdeki (b) quiet hamle m'in continuation history puanı. Önceki hamle
    // yoksa (kök ya da null move ardından) 0.
    int cont_score(const Board& b, Move m, int ply) const {
        if (ply < 1 || stack[ply - 1].piece == kNoPiece)
            return 0;
        const StackEntry& prev = stack[ply - 1];
        const int pc = piece_index(b.side_to_move, b.type_on(m.from()));
        return (*tb.cont_hist)[prev.piece][prev.to][pc][m.to()];
    }

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

    int  negamax(const Board& b, int depth, int alpha, int beta, int ply,
                 bool null_allowed = true, Move excluded = Move());
    int  quiescence(const Board& b, int alpha, int beta, int ply);
    int  score_move(const Board& b, Move m, Move tt_move, int ply) const;
    void update_quiet_stats(const Board& b, Move m, int ply, int depth,
                            const Move* quiets, int nq);

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
        const int mvv_lva = static_cast<int>(victim) * 16 - static_cast<int>(aggressor);
        if (mt == PROMOTION)
            // Promosyonlar SEE'den muaf: SEE yeni taşı modellemez, promosyon zaten güçlü.
            // (Yakalama-promosyon dahil; mevcut yüksek banttaki davranış aynen korunur.)
            return kScoreCapture + mvv_lva + kScorePromo +
                   static_cast<int>(m.promotion_type());  // vezir promosyonu en güçlü
        // Promosyon-olmayan gerçek yakalama: SEE işaretiyle iki banda ayır. Kayıplı
        // yakalama (savunmalı taşa vurma, see<0) quiet history bandının ALTINA iner;
        // iyi/eşit yakalama (see>=0) mevcut yüksek bantta kalır. Her iki bant da kendi
        // içinde MVV-LVA sıralı. En passant see() içinde doğru ele alınır (var olan test).
        const int base = (see(b, m) < 0) ? kScoreBadCapture : kScoreCapture;
        return base + mvv_lva;
    }

    // Quiet hamle: önce killer, sonra history. Quiet puanı iki bağımsız sinyalin
    // toplamı: main history (renk/from/to) + continuation history (önceki hamlenin
    // bağlamı). Toplam history bandına kırpılır ki killer bandının altında kalsın
    // (ayrık bant yapısı korunur). cont_hist sıfırken skor eski davranışla birebir.
    if (m == tb.killers[ply][0]) return kScoreKiller1;
    if (m == tb.killers[ply][1]) return kScoreKiller2;
    const int s = tb.history[b.side_to_move][m.from()][m.to()] + cont_score(b, m, ply);
    return std::clamp(s, -kHistoryMax, kHistoryMax);
}

// Beta kesmesi yapan quiet hamleyi (m) ve bu düğümde m'den önce aranmış ama
// kesme yapmayan quiet hamleleri (quiets[0..nq)) killer/history'ye işler.
// History gravity: m ödül (+bonus), diğerleri ceza (-bonus). Böylece history
// işaretli/merkezli olur — sürekli başarısız quiet'ler negatife düşer, iyi
// olanlar pozitife çıkar (ayrım güçlenir; history-LMR ileride bunu kullanabilir).
// Aynı ödül/ceza şeması continuation history'ye de (önceki hamlenin bağlamında)
// uygulanır; önceki hamle yoksa (kök / null move ardı) yalnız main güncellenir.
void Searcher::update_quiet_stats(const Board& b, Move m, int ply, int depth,
                                  const Move* quiets, int nq) {
    // Killer: kesme yapan m'i öne al (zaten 1. killer değilse).
    if (tb.killers[ply][0] != m) {
        tb.killers[ply][1] = tb.killers[ply][0];
        tb.killers[ply][0] = m;
    }
    // Derin kesmeler daha değerli. Ölçek çarpanı (kHistoryBonusScale) sıralamayı
    // DEĞİŞTİRMEZ (göreli sıra korunur, doğrulandı: taze aramada düğüm sayısı birebir
    // aynı); iki iş için var: (a) history-LMR'nin bölenine granülarite kazandırmak,
    // (b) age()'deki h/=2'nin küçük tamsayıları yok etmesini önlemek — ölçeksiz
    // tipik değerler 3-100 arasıydı, yarılama sinyali söndürmüyor siliyordu.
    const int   bonus = depth * depth * kHistoryBonusScale;
    const Color us    = b.side_to_move;

    // Continuation history bağlamı: bu düğüme götüren (bir önceki) hamle.
    const bool  has_prev = (ply >= 1 && stack[ply - 1].piece != kNoPiece);
    const int   pp = has_prev ? stack[ply - 1].piece : 0;
    const Square pt = has_prev ? stack[ply - 1].to : A1;

    // m'i ödüllendir; m'den önce boşuna aranan quiet'leri cezalandır.
    add_history(tb.history[us][m.from()][m.to()], bonus);
    if (has_prev)
        add_history((*tb.cont_hist)[pp][pt][piece_index(us, b.type_on(m.from()))][m.to()],
                    bonus);

    for (int k = 0; k < nq; ++k) {
        Move q = quiets[k];
        if (q == m) continue;
        add_history(tb.history[us][q.from()][q.to()], -bonus);
        if (has_prev)
            add_history(
                (*tb.cont_hist)[pp][pt][piece_index(us, b.type_on(q.from()))][q.to()],
                -bonus);
    }
}

// LMR indirim tablosu: derinlik ve hamle sırası (kaçıncı hamleyi arıyoruz)
// arttıkça indirim artar. İyi move ordering'de geç gelen quiet hamlelerin
// alpha'yı geçmesi düşük olasılık, o yüzden azaltılmış derinlikte aranırlar.
// r = 0.75 + ln(depth)*ln(move)/2.25 (CPW/Stockfish tarzı; bölen SPRT ile ayarlanır).
// Tablo bir kez hesaplanır.
int lmr_reduction(int depth, int move_num) {
    static const auto table = [] {
        std::array<std::array<std::int8_t, 64>, 64> t{};
        for (int d = 1; d < 64; ++d)
            for (int m = 1; m < 64; ++m)
                t[d][m] = static_cast<std::int8_t>(
                    0.75 + std::log(d) * std::log(m) / 2.25);
        return t;
    }();
    return table[std::min(depth, 63)][std::min(move_num, 63)];
}

// --- History-tabanlı LMR indirim ayarı ---
// Birleşik history sinyali (main + continuation) taban indirimi ayarlar: iyi geçmişi
// olan quiet daha az, kötü olan daha çok indirilir. Kelepçe (±kLmrStatMax ply) aşırı
// indirimi yapısal olarak sınırlar — yalnız main history ile denenen önceki form
// (4520bc1) bu frenden yoksundu ve malus ile çifte sayım yapıp over-reduce ediyordu.
//
// Bölen ÖLÇÜLEREK seçildi (kalıcı tablolarla, gerçek oyun dizisinde LMR'ye giren
// quiet'lerin |stat| dağılımı): ortalama ~224, maks 5-10k, %35-79'u tam sıfır.
// 512 -> |stat|>=512 olanlar (~%7-11) 1 ply, >=1024 olanlar 2 ply oynatır; sinyali
// olmayan çoğunluk hiç etkilenmez. Bölen/kelepçe SPRT ile ayarlanabilir.
constexpr int kLmrStatDiv = 512;
constexpr int kLmrStatMax = 2;

// --- Futility ailesi parametreleri (santipiyon) ---
// Reverse futility (static null move): sığ düğümde static_eval, beta'yı
// kRfpMargin*depth kadar aşıyorsa dal budanır.
constexpr int kRfpMaxDepth = 6;
constexpr int kRfpMargin   = 80;
// Futility pruning: sığ düğümde static_eval + kFutilityMargin[depth] bile alpha'ya
// ulaşamıyorsa quiet hamleler aranmaz. İndeks = depth (0 zaten quiescence'a gider).
constexpr int kFutilityMaxDepth  = 3;
constexpr int kFutilityMargin[4] = {0, 150, 250, 400};

// --- Razoring parametreleri (santipiyon) ---
// Sığ düğümde static_eval, alpha'yı kRazorMargin[depth] kadar aşağıdan bile
// yakalayamıyorsa: tam-derinlik aramaya girmeden qsearch ile doğrula; qsearch de
// alpha'nın altındaysa dalı buda (fail-low). RFP'nin fail-low aynası.
constexpr int kRazorMaxDepth  = 3;
constexpr int kRazorMargin[4] = {0, 300, 500, 700};  // SPRT ile ayarlanabilir

// --- Null move pruning parametreleri ---
// Rakibe "bedava hamle" verip azaltılmış derinlikte beta etrafında ararız. İndirim
// R artık dinamik: taban + derinlik (depth/kNullDepthDiv) + eval'in beta'yı ne kadar
// aştığı ((eval-beta)/kNullEvalDiv, kNullEvalMaxR ile tavanlı). Kapı `eval >= beta`
// (eval-beta)'yı >= 0 garanti eder. Sabitler ilk elle-seçim, SPRT/SPSA ile
// ayarlanabilir (Blok 4/16).
constexpr int kNullMinDepth = 3;    // bu derinliğin altında null denenmez
constexpr int kNullBaseR    = 3;    // taban indirim
constexpr int kNullDepthDiv = 3;    // derinliğe bağlı ek indirim böleni (depth/3)
constexpr int kNullEvalDiv  = 200;  // (eval-beta) başına ek indirim böleni
constexpr int kNullEvalMaxR = 3;    // eval-tabanlı ek indirim tavanı

// --- LMP (late move pruning / move-count) parametreleri ---
// Sığ, çekte-olmayan düğümde, iyi sıralamada belli sayıdan sonraki quiet
// hamleler statik eval'e bakılmaksızın atlanır. Eşik derinlikle kare-yasası
// büyür (sığ derinlik = agresif budama). Bölen/max SPRT ile ayarlanabilir.
constexpr int kLmpMaxDepth = 8;
inline int lmp_count(int depth) { return 3 + depth * depth; }

// --- SEE budaması (main search) parametreleri (santipiyon) ---
// Sığ düğümde, SEE'si çok kötü olan hamleler (hem yakalama hem quiet) aranmadan
// budanır. Yakalama için: savunmalı taşa umutsuz vurma (see çok negatif). Quiet
// için: taşı asan hamle (see negatif; see() sessiz hamleyi de değerlendirir).
// Eşik derinlikle büyür -> derinde daha az agresif. Sabitler ilk elle-seçim,
// SPRT/SPSA ile ayarlanabilir (Stockfish mertebesi).
constexpr int kSeeMaxDepth      = 8;
constexpr int kSeeCaptureMargin = 20;  // yakalama eşiği: see < -kSeeCaptureMargin*depth*depth
constexpr int kSeeQuietMargin   = 65;  // quiet eşiği:    see < -kSeeQuietMargin*depth

// --- Delta pruning (quiescence) parametresi (santipiyon) ---
// qsearch'te, çekte değilken, bir yakalama en iyi ihtimalle bile (stand_pat +
// alınan taşın materyal değeri + marj) alpha'yı geçemiyorsa aranmadan atlanır.
// Marj (~bir piyondan biraz fazla) pozisyonel salınımı örter. SPRT/SPSA adayı.
constexpr int kDeltaMargin = 200;

// --- History-tabanlı quiet budaması parametreleri ---
// Sığ, çekte-olmayan düğümde, birleşik history sinyali (main + cont) eşiğin
// çok altında olan quiet, çek-vermeyen hamleler aranmadan budanır. LMP'nin
// (sayı-tabanlı) ve SEE-quiet budamasının (materyal-tabanlı) içerik-tabanlı
// tamamlayıcısı: "bu hamle bu bağlamda geçmişte hep başarısız oldu" boyutu.
// Aynı sinyali history-LMR (kLmrStatDiv) indirim için kullanıyor; burada budama
// eşiği. Eşik derinlikle lineer büyür (sığda agresif). Sabitler ÖLÇÜLEREK seçildi
// (LMR'ye giren quiet'lerin |stat| dağılımı: ort ~224, maks 5-10k), SPRT/SPSA ile
// ayarlanabilir.
constexpr int kHistPruneMaxDepth = 4;      // yalnız depth <= 4
constexpr int kHistPruneMargin   = 2000;   // eşik: stat < -kHistPruneMargin*depth

// --- Singular extension parametreleri ---
// TT hamlesi, azaltılmış derinlikte tt_move'u DIŞLAYAN bir doğrulama aramasında
// diğer TÜM hamlelerden belirgin iyiyse (kanıtlanmış singular) o hattı 1 ply uzat.
// Yalnız derin, güvenilir LOWER/EXACT girdili düğümlerde. Sabitler ilk elle-seçim,
// SPRT/SPSA ile ayarlanabilir.
constexpr int kSingularMinDepth    = 8;  // yalnız depth >= 8 düğümlerde
constexpr int kSingularDepthMargin = 3;  // tte.depth >= depth-3 (girdi güvenilir olmalı)
constexpr int kSingularMargin      = 2;  // singularBeta = ttValue - 2*depth (cp)

// --- IIR (Internal Iterative Reduction) parametreleri ---
// TT hamlesi olmayan (miss ya da move'suz hit) yeterince derin düğümde iyi bir
// sıralama ipucu yoktur -> tam derinlik aramak pahalı. Derinliği 1 azalt; azaltılmış
// arama ucuzdur ve bir TT hamlesi üretir, üst iterasyon onu kullanır. İlk elle-seçim,
// SPRT/SPSA ile ayarlanabilir (Blok 4/16).
constexpr int kIirMinDepth = 4;

int Searcher::negamax(const Board& b, int depth, int alpha, int beta, int ply,
                      bool null_allowed, Move excluded) {
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
    if (ply > seldepth) seldepth = ply;  // seldepth raporlama (arama davranışını etkilemez)
    pv_len[ply] = ply;  // bu ply için PV başlangıçta boş

    // Legallik bağlamı düğüm başına BİR KEZ. Üretim onun maskeleriyle doğrudan legal
    // hamle çıkarır; ayrıca "çekte miyiz?" sorusu ctx.checkers'tan bedavaya cevaplanır
    // (eskiden burada + terminal_score'da iki ayrı is_square_attacked çağrısı vardı).
    const MoveGenContext ctx = make_context(b);
    const Color us       = b.side_to_move;
    const bool  in_check = ctx.checkers != 0;

    MoveList ml;
    generate_legal(b, ml, ctx);

    // Terminal düğüm: legal hamle yok -> çekteysek mat, değilsek pat.
    if (ml.size() == 0)
        return in_check ? -MATE + ply : 0;

    // Tekrar (repetition) beraberliği: kökte (ply 0) uygulanmaz — kök daima bir
    // hamle üretmeli. Arama, tekrarı 0 skorladığından önde olan taraf tekrar
    // hattını reddeder, geride olan (perpetual vb.) onu kurtarıcı olarak arar.
    // 50-hamle ile birlikte TT sondasından ÖNCE: bu beraberlikler Zobrist
    // anahtarında yok, TT kesmesi onları gözden kaçırabilirdi.
    // excluded (singular doğrulama) iken bu kısayollar ATLANIR: aynı `b`'yi tt_move'suz
    // yeniden arıyoruz, taze düğüm değiliz. Dış düğüm b.key'i yığına zaten push etti;
    // is_repetition(b) burada onu bulup "tekrar -> 0" false-positive dönerdi (kritik bug).
    // 50-hamle de dış düğümce zaten geçildi.
    if (excluded == Move()) {
        if (ply > 0 && is_repetition(b))
            return 0;

        // 50-hamle beraberliği: TT sondasından ÖNCE. halfmove_clock Zobrist
        // anahtarına dahil değil; TT kesmesi bu beraberliği gözden kaçırabilirdi.
        if (b.halfmove_clock >= 100)
            return 0;
    }

    // Horizon: statik eval yerine sessiz-arama (quiescence) — yakalama zincirleri
    // sessizleşene kadar aranır, böylece "yarım kalmış" taş alışverişleri doğru
    // değerlendirilir (horizon effect azalır). Terminal/50-hamle yukarıda ele
    // alındı; buraya yalnızca sessiz olmayabilecek gerçek yaprak düğümler gelir.
    if (depth == 0 || ply >= MAX_PLY - 1)
        return quiescence(b, alpha, beta, ply);

    // --- Mate distance pruning ---
    // Kökten ply uzaklıkta olası en iyi mat (MATE - (ply+1)) beta'dan iyi değilse
    // ya da en kötü mated (-MATE + ply) alpha'dan kötü değilse pencereyi daralt;
    // çakışırsa kes. Yalnız pencere mat sınırlarına yakınken (mat hatları) ısırır
    // -> non-mate aramada bounds değişmez, sonuç birebir korunur. Kök (ply 0)
    // dokunulmaz. score_to_tt/from_tt ply-normalizasyonuyla tutarlı (aynı MATE-ply
    // deseni). excluded (singular) iken tt_value/singular_beta mat-olmayan olduğundan
    // no-op -> ayrı guard gerekmez.
    if (ply > 0) {
        alpha = std::max(alpha, -MATE + ply);
        beta  = std::min(beta,  MATE - (ply + 1));
        if (alpha >= beta)
            return alpha;
    }

    // --- Transposition table sondası ---
    TTEntry tte;
    const bool tt_hit  = TT.probe(b.key, tte);
    const Move tt_move = tt_hit ? tte.move : Move();

    // Kökte (ply == 0) kesme yapma: kök daima tam aranmalı ki bestmove ve PV
    // güvenilir olsun. Yeterince derin bir girişte sınıra göre kes. excluded iken
    // ATLANIR: aynı key'in girdisi (LOWER, ttValue) azaltılmış doğrulama derinliğinde
    // tte.depth >= depth sağlar ve anında ttValue döndürür -> singular asla ateşlemez.
    if (excluded == Move() && tt_hit && ply > 0 && tte.depth >= depth) {
        int s = score_from_tt(tte.score, ply);
        if (tte.bound() == Bound::EXACT)                 return s;
        if (tte.bound() == Bound::LOWER && s >= beta)    return s;
        if (tte.bound() == Bound::UPPER && s <= alpha)   return s;
    }

    // Bu düğümün anahtarını arama yığınına ekle (çocuklar tekrarı görebilsin);
    // RAII ile fonksiyon çıkışında (abort dahil her yolda) çıkarılır. excluded iken
    // push etme: dış düğüm b.key'i zaten yığına koydu, çift-push çocukların tekrar
    // taramasını bozardı.
    std::optional<KeyGuard> key_guard;
    if (excluded == Move())
        key_guard.emplace(keys, b.key);

    // Futility ailesi için statik eval: çekteyken anlamsız (eval gürültülü, kaçış
    // zorunlu), yalnız çekte değilken hesaplanır. RFP + futility pruning paylaşır.
    //
    // TT'de bu pozisyonun ham eval'i varsa evaluate() çağrılmaz. Davranış-koruyan:
    // evaluate() saf bir fonksiyondur (yalnız Board okur), giriş de aynı anahtarla
    // doğrulanmıştır -> okunan değer hesaplanacak değerin birebir aynısı. Yalnız hız.
    const int static_eval = in_check                            ? 0
                          : (tt_hit && tte.eval != kEvalNone)   ? tte.eval
                          :                                       evaluate(b);

    // Budama kapılarının kullandığı eval: TT skoru, statik eval'den daha bilgili
    // bir tahmindir (bir aramadan gelir, statik terimlerden değil). Yalnız sınırın
    // izin verdiği YÖNDE kullanılır: LOWER gerçek değerin altına inmez (eval'i
    // yukarı çeker), UPPER üstüne çıkmaz (aşağı çeker), EXACT doğrudan geçer.
    // Mat skorları dışarıda: RFP `return eval` yapıyor, sahte mat skoru sızmasın.
    //
    // Rafine değer TT'ye ASLA yazılmaz (store ham static_eval'i yazar): yazılsaydı
    // sonraki sonda rafineyi tekrar rafine eder, hata birikirdi. Blok 1/3'ün
    // improving'i de ham değeri isteyecek.
    int eval = static_eval;
    if (!in_check && tt_hit) {
        const int s = score_from_tt(tte.score, ply);
        if (!is_mate_score(s) &&
            (tte.bound() == Bound::EXACT ||
             (tte.bound() == Bound::LOWER && s > eval) ||
             (tte.bound() == Bound::UPPER && s < eval)))
            eval = s;
    }

    // --- Razoring ---
    // Sığ düğümde static_eval, alpha'nın kRazorMargin[depth] kadar altındaysa bu
    // dal büyük olasılıkla fail-low. Tam-derinlik aramaya girmek yerine qsearch
    // ile doğrula: qsearch skoru da alpha'yı geçemiyorsa dalı buda (döneni
    // fail-low olarak yay). RFP'nin fail-low aynası; ikisi karşılıklı dışlayıcı
    // (eval aynı anda hem beta'yı hem alpha'yı aşamaz). Koşullar RFP/
    // futility ile aynı desende: çekte değil, kökte değil (ply>0), sığ derinlik,
    // mat penceresi değil. Taktik/yakalama qsearch'te hâlâ görülür -> budanmaz.
    if (!in_check && ply > 0 && depth <= kRazorMaxDepth && !is_mate_score(alpha) &&
        eval + kRazorMargin[depth] <= alpha) {
        int score = quiescence(b, alpha, beta, ply);
        if (score <= alpha)
            return score;  // qsearch doğruladı: fail-low, dalı buda
    }

    // --- Reverse futility pruning (RFP / static null move) ---
    // Sığ düğümde eval, beta'yı kRfpMargin*depth kadar aşıyorsa: rakip
    // normalde bu farkı tek bir alt-ağaçta telafi edemez -> dalı buda (fail-soft,
    // eval >= beta olduğundan geçerli fail-high). Koşullar: çekte değil,
    // kökte değil (ply>0), sığ derinlik, mat penceresi değil (beta=INF dahil).
    // TT'ye yazılmaz (null move deseni; sahte skor sızmasın).
    if (!in_check && ply > 0 && depth <= kRfpMaxDepth && !is_mate_score(beta) &&
        eval - kRfpMargin * depth >= beta)
        return eval;

    // --- Null move pruning ---
    // Sıradaki tarafa "bedava hamle" (pass) ver; oluşan pozisyonu azaltılmış
    // derinlikte (R) beta etrafında null-window ile ara. Rakip iki kez üst üste
    // oynamış gibi olmasına rağmen skor hâlâ >= beta ise, bu düğüm zaten çok iyi
    // demektir -> dalı buda. Koşullar (ucuzdan pahalıya, kısa devre için):
    //   - null_allowed: üst üste iki null yasak (null-child'a false geçilir).
    //   - ply > 0     : kökte asla (kök gerçek hamle üretmeli).
    //   - depth >= kNullMinDepth: çok sığında budama gürültülü.
    //   - beta mat penceresi değil (beta=INF dahil): mat aramasında null yok.
    //   - piyon-dışı materyal var: zugzwang koruması (yalnız şah+piyon'da kapalı).
    //   - çekte değil : çekteyken pass edilemez.
    //   - eval >= beta: statik eval zaten beta'nın altındaysa null boşuna denenir;
    //     ayrıca dinamik R'nin (eval-beta) terimini >= 0 garanti eder.
    if (null_allowed && ply > 0 && depth >= kNullMinDepth && !is_mate_score(beta) &&
        b.has_non_pawn_material(us) && !in_check && eval >= beta) {
        // Dinamik indirim: taban + derinlik + eval'in beta'yı ne kadar aştığı.
        const int R = kNullBaseR + depth / kNullDepthDiv
                    + std::min((eval - beta) / kNullEvalDiv, kNullEvalMaxR);
        const int null_depth = std::max(depth - R, 1);  // en az 1 ply reduced arama
        Board next = b;
        next.make_null_move();
        // Null-child continuation history bağlamı görmemeli: bu ply'de gerçek bir
        // hamle oynanmadı, bayat bir bağlam okumasın.
        stack[ply] = StackEntry{};
        // Null-child'ın anahtarı, recursive negamax'ın kendi KeyGuard'ıyla push edilir.
        int score = -negamax(next, null_depth, -beta, -beta + 1, ply + 1,
                             /*null_allowed=*/false);
        if (aborted)
            return 0;  // süre doldu: sonuç yukarıda yok sayılır (fn başıyla tutarlı)
        if (score >= beta)
            return beta;  // fail-high: dalı buda (fail-hard; sahte mat skoru sızmasın)
    }

    // --- Internal Iterative Reduction (IIR) ---
    // TT hamlesi yoksa sıralama ipucumuz yok; derinliği 1 azalt (bkz. kIirMinDepth notu).
    // Kökte (ply 0) ASLA: kök daima tam aranmalı (bestmove/PV güvenilir olsun) — dosyadaki
    // tüm budama/indirim kapılarıyla tutarlı. Gerçek oyunda kök zaten iterative deepening'den
    // bir tt_move taşır, bu yüzden bu kısıt kayıp değil. tt_move VARKEN singular extension
    // devreye girer -> ikisi karşılıklı dışlayıcı, etkileşim yok. Azaltılmış derinlik buradan
    // sonra move döngüsündeki LMR/LMP/futility kapılarına da yansır (standart, istenen
    // davranış). RFP/razoring/null yukarıda kaldığı için orijinal derinlikle çalışır.
    if (ply > 0 && depth >= kIirMinDepth && tt_move == Move())
        depth -= 1;

    // Tüm hamleleri skorla (TT hamlesi, MVV-LVA, killer, history). Her iterasyonda
    // kalanların en yükseğini öne çekiyoruz (lazy selection sort): iyi sıralamada
    // beta kesmesi çoğunlukla ilk birkaç hamlede olur, kalanı sıralamak boşa gider.
    int scores[256];
    for (int i = 0; i < ml.count; ++i)
        scores[i] = score_move(b, ml.moves[i], tt_move, ply);

    const int alpha_orig = alpha;
    int  best      = -INF;
    Move best_move = Move();
    int  moves_searched = 0;  // futility: en az bir hamle tam arandı mı

    // History malus için: bu düğümde aranmış (kesme yapmadan geçilmiş) quiet
    // hamleler. Beta kesmesinde kesen hamle ödül, bunlar ceza alır. 64 tavan
    // (nadiren aşılır; aşarsa fazlası cezasız kalır, zararsız).
    Move quiets_searched[64];
    int  nquiets = 0;

    // --- Futility pruning kapısı (node seviyesi) ---
    // Sığ, çekte-olmayan, mat-olmayan düğümde eval + margin bile alpha'ya
    // ulaşamıyorsa quiet hamleler (materyali pek değiştirmez) alpha'yı geçemez ->
    // döngüde çek vermeyen quiet hamleler aranmadan atlanır. alpha burada düğüm
    // giriş değerinde (döngü henüz değiştirmedi).
    const bool can_futility =
        !in_check && ply > 0 && depth <= kFutilityMaxDepth &&
        !is_mate_score(alpha) &&
        eval + kFutilityMargin[depth] <= alpha;

    // --- LMP (late move pruning) kapısı (node seviyesi) ---
    // Sığ, çekte-olmayan, mat-olmayan düğümde belli sayıdan sonraki quiet, çek
    // vermeyen hamleler eval'e bakılmaksızın sıra numarasına göre budanır.
    const bool can_lmp =
        !in_check && ply > 0 && depth <= kLmpMaxDepth &&
        !is_mate_score(alpha) && !is_mate_score(beta);

    // --- SEE budaması kapısı (node seviyesi) ---
    // Sığ, çekte-olmayan, mat-olmayan düğümde SEE'si eşiğin altında olan hamleler
    // (yakalama + quiet) döngüde budanır. Futility/LMP (quiet-only) ile tamamlayıcı:
    // SEE budaması yakalamaları da kapsar; Commit 1'in sıralamasıyla tamamlayıcı
    // (o tüm derinlikte reorder eder, bu sığ derinlikte eler).
    const bool can_see_prune =
        !in_check && ply > 0 && depth <= kSeeMaxDepth &&
        !is_mate_score(alpha) && !is_mate_score(beta);

    // --- History budaması kapısı (node seviyesi) ---
    // Sığ, çekte-olmayan, mat-olmayan düğümde birleşik history'si çok kötü quiet,
    // çek-vermeyen hamleler döngüde budanır. is_mate_score guard'ları PV-kök geniş
    // penceresini (beta=INF) doğal olarak dışlar (futility/LMP/SEE ile aynı desen).
    const bool can_hist_prune =
        !in_check && ply > 0 && depth <= kHistPruneMaxDepth &&
        !is_mate_score(alpha) && !is_mate_score(beta);

    // Çek-verme bağlamı TEMBEL kurulur. gives_check yalnızca i >= 1'de sorulur
    // (futility moves_searched>0 ister, LMP eşiği >= 4, LMR i >= 2) — yani ilk
    // hamlede beta kesmesi yapan düğümlerde, ki iyi sıralamada çoğunluk onlardır,
    // hiç ödenmez.
    CheckInfo ci;
    bool      ci_ready = false;

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

        // Singular doğrulama araması dışlanan hamleyi (tt_move) atlar.
        if (m == excluded)
            continue;

        // quiet / gives_ck bir kez hesaplanır (futility + LMP + LMR paylaşır).
        // gives_ck artık ÇOCUK TAHTA KURULMADAN belirleniyor — bu, aşağıdaki iki
        // budamanın kopya + do_move bedelini tamamen ortadan kaldırmasını sağlıyor
        // (eskiden budanan hamle bile tam bir Board kopyası ödüyordu).
        const bool quiet = !is_capture(b, m) && m.type() != PROMOTION;
        bool gives_ck = false;
        if (i > 0) {
            if (!ci_ready) {
                ci       = make_check_info(b);
                ci_ready = true;
            }
            gives_ck = gives_check(b, m, ci);
        }

        // Futility pruning: en az bir hamle tam arandıysa (moves_searched>0 ->
        // best güvenilir), çek vermeyen quiet umutsuz hamleyi hiç arama. i==0
        // (PVS ilk hamle) daima aranır -> fail-low düğümde bile bir hamle/PV kalır.
        if (can_futility && moves_searched > 0 && quiet && !gives_ck)
            continue;
        // Late move pruning: yeterince geç sıralanmış quiet, çek-vermeyen hamleyi
        // eval'e bakmadan atla. lmp_count(depth) >= 4 olduğundan en az PV +
        // birkaç hamle daima aranır (moves_searched eşiğe ulaşana dek).
        if (can_lmp && moves_searched >= lmp_count(depth) && quiet && !gives_ck)
            continue;
        // History budaması: birleşik sinyali (main + cont) eşiğin çok altında olan
        // quiet, çek-vermeyen hamleyi hiç arama. moves_searched>0 (i==0/PV daima
        // aranır) + quiet + !gives_ck (taktik/çek korunur). Ham (kırpılmamış) stat —
        // LMR'deki (aşağıda) ile aynı hesap: score_move'un bant kırpması sıralama
        // içindi, burada sinyalin tam genliği lazım. SEE budamasından ÖNCE: history
        // iki dizi okuması (ucuz), SEE swap algoritması (pahalı) — kötü-history hamle
        // SEE çağrısına hiç girmesin.
        if (can_hist_prune && moves_searched > 0 && quiet && !gives_ck) {
            const int stat = tb.history[us][m.from()][m.to()] + cont_score(b, m, ply);
            if (stat < -kHistPruneMargin * depth)
                continue;
        }
        // SEE budaması: SEE'si çok kötü hamleyi (yakalama ya da quiet) hiç arama.
        // moves_searched>0 (ilk/PVS hamle daima aranır) + !gives_ck (çek veren taktik
        // hatlar korunur) + promosyon hariç (see() promosyonu desteklemez). Yakalama
        // için eşik depth² ile büyür (derin taktik alışverişe tolerans), quiet için
        // lineer (asma hamlesi zaten sığda budanmalı).
        if (can_see_prune && moves_searched > 0 && !gives_ck && m.type() != PROMOTION) {
            const int threshold = quiet ? -kSeeQuietMargin   * depth
                                        : -kSeeCaptureMargin * depth * depth;
            if (see(b, m) < threshold)
                continue;
        }
        ++moves_searched;

        // Budama kararları geçildi: ancak ŞİMDİ çocuk tahtayı kur.
        Board next = b;
        next.do_move(m);

        // History malus: aranan quiet hamleyi kaydet (kesme olursa bunlardan
        // kesmeyi yapan ödül, kalanlar ceza alır).
        if (quiet && nquiets < 64)
            quiets_searched[nquiets++] = m;

        // NOT: Naif/SEE-kapılı check extension (çek veren TÜM hamleleri uzat) denendi,
        // SPRT'de NÖTR -> rafa kaldırıldı; çok-budayan yığında çeklerin taktik değeri
        // zaten yakalanıyordu. Singular extension (aşağıda) bunun "doğru hali": kör
        // değil, yalnız kanıtlanmış-tekil tt_move'u uzatır.

        // --- Singular extension ---
        // tt_move'u aramadan önce: TT skorunun biraz altında (singularBeta), tt_move'u
        // DIŞLAYAN, azaltılmış derinlikte null-window doğrulama. Hiçbir alternatif
        // singularBeta'ya ulaşamıyorsa (fail-low) tt_move tekildir -> 1 ply uzat.
        // Yalnız: excluded değil (iç içe singular yok), m == tt_move (i==0'ı sağlamlaştırır;
        // TT collision'da tt_move illegal olabilir), derin düğüm, güvenilir LOWER/EXACT
        // girdi, mat-olmayan ttValue.
        int extension = 0;
        if (excluded == Move() && tt_hit && m == tt_move && ply > 0 &&
            depth >= kSingularMinDepth &&
            tte.depth >= depth - kSingularDepthMargin &&
            (tte.bound() == Bound::LOWER || tte.bound() == Bound::EXACT)) {
            const int tt_value = score_from_tt(tte.score, ply);
            if (!is_mate_score(tt_value)) {
                const int singular_beta  = tt_value - kSingularMargin * depth;
                const int singular_depth = (depth - 1) / 2;
                // b üzerinde, tt_move DIŞLANARAK, null-window [sb-1, sb] doğrulama.
                const int s = negamax(b, singular_depth, singular_beta - 1, singular_beta,
                                      ply, /*null_allowed=*/false, /*excluded=*/tt_move);
                if (!aborted && s < singular_beta)
                    extension = 1;  // hiçbir alternatif yaklaşamadı -> tekil
            }
        }
        const int new_depth = depth - 1 + extension;

        // Continuation history bağlamı: çocuk düğümler "bu ply'de hangi taş nereye
        // oynandı" bilgisini stack[ply]'den okur. Promosyonda hareket eden taş
        // piyondur (b hamle öncesi tahta).
        stack[ply] = StackEntry{piece_index(us, b.type_on(m.from())), m.to()};

        // PVS (Principal Variation Search): ilk (en iyi sıralanan) hamle tam
        // pencereyle aranır ve PV adayı kabul edilir. Kalan hamleler önce
        // null-window (scout) [-alpha-1, -alpha] ile aranır — bu yalnızca "bu
        // hamle alpha'dan iyi mi?" sorusunu ucuza (daha çok budamayla) yanıtlar.
        // Scout alpha'yı geçerse (ve beta'nın altındaysa) varsayım yanlıştı: gerçek
        // değeri bulmak için tam pencereyle yeniden aranır. İyi move ordering'de
        // re-search nadirdir, net kazanç düğüm sayısında. (LMR'nin oturacağı çerçeve.)
        int score;
        if (i == 0) {
            score = -negamax(next, new_depth, -beta, -alpha, ply + 1);
        } else {
            // --- LMR (Late Move Reductions) ---
            // Geç sıralanan quiet, çek-vermeyen hamleler büyük olasılıkla alpha'yı
            // geçmez; azaltılmış derinlikte (scout) aranırlar. Beklenmedik şekilde
            // alpha'yı geçerse indirim yanlıştı -> tam derinlikte yeniden aranır.
            // İndirim uygulanmaz: ilk iki hamle (PV + ilk scout), yakalama/promosyon,
            // çekteyken (kaçış), çek veren hamle, killer'lar.
            int reduction = 0;
            if (depth >= 3 && i >= 2 && quiet && !in_check && !gives_ck &&
                m != tb.killers[ply][0] && m != tb.killers[ply][1]) {
                reduction = lmr_reduction(depth, i);
                // Sıra numarasının verdiği taban indirimi, hamlenin birleşik history
                // sinyaliyle ayarla. Ham (kırpılmamış) toplam kullanılır: score_move'daki
                // bant kırpması sıralama içindi, burada sinyalin tam genliği lazım.
                const int stat = tb.history[us][m.from()][m.to()] + cont_score(b, m, ply);
                reduction -= std::clamp(stat / kLmrStatDiv, -kLmrStatMax, kLmrStatMax);
                if (reduction > depth - 2) reduction = depth - 2;  // reduced >= 1 ply
                if (reduction < 0)         reduction = 0;          // negatif indirim yok
            }

            // Azaltılmış derinlikte null-window (scout) arama.
            score = -negamax(next, new_depth - reduction, -alpha - 1, -alpha, ply + 1);
            // İndirimli scout alpha'yı geçtiyse indirim yanlıştı: tam derinlikte
            // null-window ile yeniden ara (PVS re-search'ten önce).
            if (!aborted && reduction > 0 && score > alpha)
                score = -negamax(next, new_depth, -alpha - 1, -alpha, ply + 1);
            // PVS: null-window alpha'yı geçip beta'nın altında kaldıysa gerçek
            // değeri bulmak için tam pencereyle yeniden ara.
            if (!aborted && score > alpha && score < beta)
                score = -negamax(next, new_depth, -beta, -alpha, ply + 1);
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
                update_quiet_stats(b, m, ply, depth, quiets_searched, nquiets);
            break;
        }
    }

    // --- Sonucu TT'ye sakla ---
    // best <= alpha_orig: hiçbir hamle alpha'yı geçmedi -> üst sınır (fail-low).
    // best >= beta      : beta kesmesi -> alt sınır (fail-high).
    // aksi              : gerçek değer (exact).
    // eval alanına HAM statik eval yazılır (çekteyken yok). Rafine edilmiş bir
    // değer yazılsaydı sonraki sonda onu tekrar rafine eder, hata birikirdi.
    // excluded (singular doğrulama) iken YAZMA: best, tt_move-hariç-en-iyi'dir =
    // düğümün gerçek değeri değil; yazılsaydı TT bozulurdu.
    if (excluded == Move()) {
        Bound bound = (best <= alpha_orig) ? Bound::UPPER
                    : (best >= beta)       ? Bound::LOWER
                    :                        Bound::EXACT;
        TT.store(b.key, depth, score_to_tt(best, ply), bound, best_move,
                 in_check ? kEvalNone : static_eval);
    }

    return best;
}

// Quiescence (sessiz-arama): negamax'ın yaprağında çağrılır. Yalnızca "gürültülü"
// hamleleri (yakalamalar + promosyonlar) arar; pozisyon sessizleşince statik eval
// döner. Böylece derinlik sınırının tam ortasındaki taş alışverişleri yanlış
// değerlendirilmez. Çekteyken stand-pat yapılmaz ve tüm legal kaçışlar aranır
// (mat/horizon doğruluğu). İyileştirme (delta pruning) sonraya bırakıldı.
//
// TT: qsearch girişleri depth 0 ile saklanır. negamax `tte.depth >= depth` (depth>=1)
// ister, yani bir qsearch girişi negamax'ı ASLA kesemez — yalnız hamle ipucu ve ham
// eval verir. Bu, qsearch TT'sinin güvenlik ağıdır.
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
    if (ply > seldepth) seldepth = ply;  // seldepth raporlama (arama davranışını etkilemez)

    if (ply >= MAX_PLY - 1)
        return evaluate(b);

    // --- TT sondası ---
    // depth >= 0 herhangi bir girişi kabul eder; derin bir negamax girişi qsearch'ten
    // daha bilgilidir, sınır anlamı pencereden bağımsızdır -> kesme daha da sağlam.
    TTEntry tte;
    const bool tt_hit = TT.probe(b.key, tte);
    if (tt_hit && tte.depth >= 0) {
        const int s = score_from_tt(tte.score, ply);
        if (tte.bound() == Bound::EXACT)                return s;
        if (tte.bound() == Bound::LOWER && s >= beta)   return s;
        if (tte.bound() == Bound::UPPER && s <= alpha)  return s;
    }

    const bool in_check =
        is_square_attacked(b, b.king_square(b.side_to_move), ~b.side_to_move);

    MoveList ml;
    int      best;
    int      raw_eval = kEvalNone;  // çekteyken statik eval yok

    if (in_check) {
        // Çekteyiz: stand-pat yasak, tüm kaçışları aramalıyız. Kaçış yoksa mat.
        // Kaçışlar gürültülü olmak zorunda değil -> tam legal üretim şart.
        generate_legal(b, ml);
        if (ml.size() == 0)
            return -MATE + ply;  // mat: saklanmaz (aşağıdaki mat filtresiyle tutarlı)
        best = -INF;
    } else {
        // Stand-pat: sıradaki taraf hiçbir şey almadan mevcut skoru "cebe atabilir"
        // (sessiz pozisyonda eval alt sınır kabul edilir). Beta'yı aşıyorsa kes.
        // Hamle üretiminden ÖNCE: stand-pat listeye bakmaz, beta kesmesinde üretilen
        // liste tamamen çöp olurdu.
        raw_eval = (tt_hit && tte.eval != kEvalNone) ? tte.eval : evaluate(b);
        int stand_pat = raw_eval;
        if (stand_pat >= beta) {
            TT.store(b.key, 0, score_to_tt(stand_pat, ply), Bound::LOWER, Move(), raw_eval);
            return stand_pat;
        }
        if (stand_pat > alpha)
            alpha = stand_pat;
        best = stand_pat;

        // Yalnız gürültülü hamleler üretilir (yakalama + tüm promosyonlar).
        // generate_legal ile aynı göreli sırada gelirler -> sıralama birebir korunur.
        generate_noisy(b, ml);
    }

    // Aranacak hamleler: çekteyken hepsi; değilse yakalama + promosyon (ml zaten
    // yalnız onları içeriyor). SEE budaması: çekte değilken, promosyon olmayan
    // yakalamalardan yalnızca kayıplı OLMAYANLAR (see >= 0) aranır — statik olarak
    // materyal kaybettiren yakalamalar (ör. savunmalı taşa kaleyle vurma) qsearch'ü
    // şişirmeden elenir. Promosyonlar SEE'den muaf (daima aranır).
    MoveList todo;
    for (Move m : ml)
        if (in_check
            || m.type() == PROMOTION
            || (is_capture(b, m) && see(b, m) >= 0))
            todo.add(m);

    // MVV-LVA (+ promosyon primi) ile skorla. TT hamlesi en öne (listede varsa;
    // TT hamlesi quiet ya da SEE ile elenmişse hiç eşleşmez, zararsız).
    const Move tt_move = tt_hit ? tte.move : Move();
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
        if (m == tt_move)
            s += 100000;
        scores[i] = s;
    }

    Move best_move = Move();

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

        // --- Delta pruning ---
        // Çekte değilken, promosyon olmayan yakalamalar: en iyi ihtimalle bile
        // (stand_pat + alınan taşın değeri + marj) çalışan alpha'yı geçemiyorsa,
        // bu yakalama umutsuzdur -> aranmadan atlanır. raw_eval bu dalda stand_pat
        // değerini tutar (çekteyken kEvalNone -> gate zaten !in_check ile kapalı).
        // Promosyon muaf (vezir ekler, materyal sıçraması delta mantığına aykırı;
        // SEE muafiyetiyle aynı). MVV-LVA sıralaması büyük kurbanları öne alır ama
        // tt_move primi sırayı bozabildiğinden güvenli `continue` (break değil).
        if (!in_check && todo.moves[i].type() != PROMOTION) {
            const Move      m      = todo.moves[i];
            const PieceType victim = (m.type() == EN_PASSANT) ? PAWN
                                                              : b.type_on(m.to());
            if (raw_eval + MaterialValue[victim] + kDeltaMargin <= alpha)
                continue;
        }

        Board next = b;
        next.do_move(todo.moves[i]);
        int score = -quiescence(next, -beta, -alpha, ply + 1);

        if (aborted)
            return best;  // yarım sonuç: TT'ye yazma

        if (score > best) {
            best      = score;
            best_move = todo.moves[i];
        }
        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;  // fail-high
    }

    // --- Sonucu TT'ye sakla (depth 0) ---
    // EXACT saklanmaz: PV-düğüm bayrağımız yok, LOWER/UPPER her pencerede güvenli
    // (fail-high -> gerçek >= best; aksi -> best tüm gürültülü hamlelerin maksimumu,
    // gerçek <= best). Mat skorları saklanmaz: ply normalizasyonunun qsearch'te
    // aynalanması bir hata sınıfı açar, kazanç küçük.
    if (!is_mate_score(best)) {
        const Bound bound = (best >= beta) ? Bound::LOWER : Bound::UPPER;
        TT.store(b.key, 0, score_to_tt(best, ply), bound, best_move, raw_eval);
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

// --- Adaptif zaman yönetimi (best-move stability) parametreleri ---
// stability = art arda kök best-move'un değişmediği tamamlanan derinlik sayısı.
// 0 (kararsız) -> soft limiti UZAT (düşünmeye devam et), yüksek -> KIS (erken dur,
// saati bankala). Ölçek tipik pozisyonda ~1.0 civarında merkezlenir; hard_ms zaten
// mutlak tavan olduğundan uzatma sınırlıdır. Sabitler ilk elle-seçim, SPRT/SPSA ile
// ayarlanabilir (Blok 4/16).
namespace {
constexpr double kTimeStabMax   = 1.5;   // stability 0'da ölçek (en uzun)
constexpr double kTimeStabStep  = 0.13;  // her kararlı derinlikte azalma
constexpr int    kTimeStabCap   = 8;     // sayaç doygunluğu
constexpr double kTimeStabFloor = 0.5;   // en agresif kısma (alt sınır)
}  // namespace

// stability -> soft limit ölçeği. Monoton azalan, [kTimeStabFloor, kTimeStabMax]
// sınırlı saf fonksiyon (search.hpp'de ilan; birim testte doğrulanır).
double time_scale(int stability) {
    int s = stability < kTimeStabCap ? stability : kTimeStabCap;
    if (s < 0) s = 0;
    double f = kTimeStabMax - kTimeStabStep * s;
    return f < kTimeStabFloor ? kTimeStabFloor : f;
}

SearchResult search(const Board& b, int depth, std::int64_t max_time_ms,
                    const std::vector<std::uint64_t>& history) {
    // Sabit derinlik: her çağrı sıfırdan tablolarla, deterministik (testlerin kapısı).
    SearchTables local;
    Searcher s(*local.impl);
    s.keys = history;  // tekrar tespiti için oyun geçmişiyle tohumla
    if (max_time_ms >= 0) {
        s.use_deadline = true;
        s.deadline = Clock::now() + std::chrono::milliseconds(max_time_ms);
    }

    int score = s.negamax(b, depth, -INF, INF, 0);

    SearchResult res;
    res.nodes    = s.nodes;
    res.seldepth = s.seldepth;
    res.aborted  = s.aborted;
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
                              const std::vector<std::uint64_t>& history,
                              SearchTables* tables) {
    // Kalıcı tablolar verildiyse aramalar arası birikimi koru, ama yaşlandır
    // (history yarılanır -> doyma yok, eski bilgi söner; killer'lar temizlenir).
    // Verilmediyse geçici, sıfırdan tablolar (testler deterministik kalır).
    SearchTables local;
    SearchTables& t = tables ? *tables : local;
    if (tables) t.impl->age();

    Searcher s(*t.impl);
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
    // Adaptif zaman yönetimi: kök best-move'un derinlikler boyunca kararlılığı.
    Move         prev_best  = Move();
    int          stability  = 0;

    for (int depth = 1; depth <= lim.max_depth; ++depth) {
        s.root_best_move  = Move();
        s.root_best_score = -INF;
        s.seldepth        = 0;  // her iterasyonun selektif erişimini ayrı raporla

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
            best.nodes    = s.nodes;
            best.seldepth = s.seldepth;
            best.aborted  = true;
            break;
        }

        // Derinlik tamamlandı: sonucu güncelle.
        prev_score    = score;
        best.score    = score;
        best.nodes    = s.nodes;
        best.seldepth = s.seldepth;
        best.aborted  = false;
        best.pv.clear();
        for (int j = 0; j < s.pv_len[0]; ++j)
            best.pv.push_back(s.pv_table[0][j]);
        best.best = best.pv.empty() ? Move() : best.pv[0];

        if (info)
            info(best, depth);

        if (is_mate_score(score))
            break;  // mat bulundu: daha derine gitmenin anlamı yok

        // Kök best-move kararlılığını güncelle (derinlik 1 daima ilk hamle -> sayaç
        // depth 2'den itibaren birikir).
        if (depth > 1 && best.best == prev_best) ++stability;
        else                                     stability = 0;
        prev_best = best.best;

        // Soft limit: hedef bütçeyi aştıysak bir sonraki (daha pahalı) derinliğe
        // başlama; elimizdeki en iyi hamleyi oyna. Timed-game modunda (adaptive_time)
        // eşik best-move kararlılığına göre ölçeklenir: kararlıysa kıs (erken dur),
        // kararsızsa uzat (hard_ms tavanına kadar). Diğer modlarda birebir statik.
        std::int64_t soft = (lim.soft_ms >= 0) ? lim.soft_ms : lim.hard_ms;
        if (soft >= 0) {
            std::int64_t eff = soft;
            if (lim.adaptive_time && lim.hard_ms > lim.soft_ms) {
                eff = static_cast<std::int64_t>(soft * time_scale(stability));
                if (eff > lim.hard_ms) eff = lim.hard_ms;  // hard tavanı aşma
            }
            if (elapsed_ms() >= eff)
                break;
        }
    }

    return best;
}

}  // namespace engine
