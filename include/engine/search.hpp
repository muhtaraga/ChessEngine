#pragma once

// Arama (search): negamax + alpha-beta budama.
//  - search()           : tek, sabit derinlikte arar (testler ve düşük seviye kullanım).
//  - search_iterative() : iterative deepening + aspiration windows + zaman yönetimi
//                         (soft/hard limit, abort'ta en iyi hamleyi koruma). UCI bunu kullanır.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "engine/board.hpp"
#include "engine/move.hpp"

namespace engine {

// Mat puanı. Daha kısa matlar tercih edilsin diye ply ile ayarlanır
// (mated skoru = -MATE + ply). MATE'e yakın |puan| bir mat dizisini gösterir.
constexpr int MATE      = 31000;
constexpr int MATE_IN_MAX = MATE - 1000;  // |puan| bunun üstündeyse mat

// Bir puanın mat puanı olup olmadığı.
inline bool is_mate_score(int score) {
    return score > MATE_IN_MAX || score < -MATE_IN_MAX;
}

// Move ordering tabloları (killer moves, history, continuation history).
//
// Bu tablolar TEK BİR ARAMADAN daha uzun yaşamalıdır: bir hamlelik aramada (tipik
// maçta ~200 ms) biriken sinyal, özellikle seyrek continuation history için, hemen
// hemen boştur. Stockfish de bunları oyun boyunca taşır ve yalnızca "ucinewgame"de
// temizler. Bu yüzden sahiplik çağırana (UCI) verildi: bir oyun için tek örnek
// tutulur, search_iterative'e verilir, ucinewgame'de clear() çağrılır.
//
// nullptr geçilirse arama kendi geçici tablolarını kullanır -> testler deterministik
// kalır (birbirine sızan durum yok). Faz 2D'de her thread kendi örneğini alır.
class SearchTables {
public:
    SearchTables();
    ~SearchTables();
    SearchTables(const SearchTables&) = delete;
    SearchTables& operator=(const SearchTables&) = delete;

    // Yeni oyun: tüm tabloları sıfırla.
    void clear();

    struct Impl;                 // tanımı search.cpp'de (tablolar iç detay)
    std::unique_ptr<Impl> impl;
};

struct SearchResult {
    Move              best;      // bulunan en iyi hamle (terminalde geçersiz)
    int               score = 0; // hamle sırası olan tarafın bakışıyla (santipiyon)
    std::uint64_t     nodes = 0; // ziyaret edilen düğüm sayısı
    int               seldepth = 0; // ulaşılan en derin ply (qsearch/extension dahil)
    std::vector<Move> pv;        // baş varyant (principal variation)
    bool              aborted = false;  // süre dolduğu için yarıda kesildi mi
};

// Verilen derinlikte pozisyonu arar. max_time_ms >= 0 ise arama o süreden
// sonra (yaklaşık, düğüm örneklemeli) kesilir ve aborted=true döner; bu durumda
// sonuç eksiktir, çağıran önceki derinliği kullanmalıdır. max_time_ms < 0 =
// zaman sınırı yok.
// history: kökten ÖNCEKİ pozisyonların Zobrist anahtarları (oyun başından köke
// dek, kök hariç). Tekrar (repetition) beraberliği tespiti için kullanılır; boş
// verilirse yalnızca arama ağacı içi tekrarlar görülür (mevcut testler için yeterli).
SearchResult search(const Board& b, int depth, std::int64_t max_time_ms = -1,
                    const std::vector<std::uint64_t>& history = {});

// Iterative deepening için zaman/derinlik sınırları.
//  - hard_ms: mutlak kesme (deadline). Bu süreye ulaşınca arama düğüm ortasında
//    kesilir. <0 -> zaman sınırı yok.
//  - soft_ms: bir derinlik tamamlandıktan sonra, geçen süre bunu aşmışsa yeni
//    (daha pahalı) derinliğe başlanmaz. Tipik olarak hedef bütçe. <0 -> yalnızca
//    hard_ms geçerli.
// Not: Derinlik 1 daima süresiz koşar ki en az bir legal hamle garanti olsun.
struct SearchLimits {
    int          max_depth = 64;
    std::int64_t soft_ms   = -1;
    std::int64_t hard_ms   = -1;
    // Dışarıdan asenkron durdurma bayrağı (UCI "stop"). nullptr değilse arama
    // bu bayrağı düzenli yoklar; true olunca düğüm ortasında kesilir. "go infinite"
    // için zaman sınırı olmadan yalnızca bu bayrakla durmayı sağlar.
    const std::atomic<bool>* stop = nullptr;
    // Adaptif zaman yönetimi: kök best-move kararlılığına göre soft limiti ölçekle.
    // Yalnız timed-game (wtime/btime) modunda true; movetime/depth/infinite/default
    // budget modlarında false (o modlarda soft==hard ya da -1, ölçekleme yanlış olur).
    bool adaptive_time = false;
    // Lazy SMP iş parçacığı sayısı. 1 -> tek thread (mevcut davranış birebir).
    // >1 -> bu kadar thread aynı kökü paylaşılan TT ile arar (vector<SearchTables*>
    // alan search_iterative overload'u ile kullanılır).
    int threads = 1;
};

// Her tamamlanan derinlikten sonra çağrılan bilgi geri-çağırması (UCI "info"
// satırı için). result o ana kadarki en iyi sonucu, depth tamamlanan derinliği verir.
using InfoCallback = std::function<void(const SearchResult& result, int depth)>;

// Iterative deepening ile arar: derinlik 1'den lim.max_depth'e, aspiration
// windows kullanarak. Zaman sınırı dolunca yarım kalan derinliği atar ama o
// derinlikte kökte gerçek bir iyileşme bulunduysa onu korur (abort'ta blunder
// önleme). info verilirse her tamamlanan derinlikte çağrılır.
// tables: oyun boyunca yaşayan move ordering tabloları (bkz. SearchTables). Verilirse
// her arama başında yaşlandırılır (history yarılanır, killer'lar temizlenir) ve
// aramalar arası birikim korunur. nullptr -> geçici, sıfırdan tablolar.
SearchResult search_iterative(const Board& b, const SearchLimits& lim,
                              const InfoCallback& info = {},
                              const std::vector<std::uint64_t>& history = {},
                              SearchTables* tables = nullptr);

// Lazy SMP overload'u: lim.threads iş parçacığı aynı kök pozisyonu paylaşılan TT
// üzerinden arar; TT yarışları + küçük move-ordering farklarıyla doğal olarak
// ıraksarlar. Ana thread (id 0) info raporlar ve sonucu döndürür; yardımcı thread'ler
// yalnız TT'yi doldurur. thread_tables: her thread için bir SearchTables (boyut >=
// lim.threads olmalı; her thread kendi örneğini alır, aralarında sızıntı yok).
// lim.threads <= 1 -> yukarıdaki tek-thread yoluna delege eder (davranış birebir).
// Determinizm YOKTUR (çok-thread): düğüm/skor iddialı testler tek-thread koşmalı.
SearchResult search_iterative(const Board& b, const SearchLimits& lim,
                              const InfoCallback& info,
                              const std::vector<std::uint64_t>& history,
                              const std::vector<SearchTables*>& thread_tables);

// Adaptif zaman yönetimi ölçeği (best-move stability). stability = kök best-move'un
// art arda değişmeden kaldığı tamamlanan derinlik sayısı. Kararsız (0) -> büyük ölçek
// (soft limiti uzat), kararlı -> küçük ölçek (kıs). Monoton azalan, sınırlı. Yalnız
// search_iterative timed-game modunda kullanır; birim testinde doğrulanabilsin diye
// ilan edilir.
double time_scale(int stability);

// Lazy SMP derinlik-atlama: yardımcı thread'lerin iterative-deepening yörüngelerini
// kaydırır. Stockfish-tarzı SkipSize/SkipPhase deseni: yardımcı thread belli
// derinlikleri atlar -> thread'ler farklı derinlik alt-kümelerini arar, ağacı daha
// çeşitli keşfeder ve paylaşılan TT'yi çeşitli-derinlikli girişlerle doldurur.
// thread_idx==0 (ana thread) ASLA atlamaz (tam schedule; depth 1 garantisi + best/PV
// korunur). Saf/deterministik; birim testinde doğrulanabilsin diye ilan edilir.
bool lazy_smp_skip(int thread_idx, int depth);

}  // namespace engine
