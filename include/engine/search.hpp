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

}  // namespace engine
