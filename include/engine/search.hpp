#pragma once

// Arama (search): negamax + alpha-beta budama.
//  - search()           : tek, sabit derinlikte arar (testler ve düşük seviye kullanım).
//  - search_iterative() : iterative deepening + aspiration windows + zaman yönetimi
//                         (soft/hard limit, abort'ta en iyi hamleyi koruma). UCI bunu kullanır.

#include <cstdint>
#include <functional>
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
SearchResult search(const Board& b, int depth, std::int64_t max_time_ms = -1);

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
};

// Her tamamlanan derinlikten sonra çağrılan bilgi geri-çağırması (UCI "info"
// satırı için). result o ana kadarki en iyi sonucu, depth tamamlanan derinliği verir.
using InfoCallback = std::function<void(const SearchResult& result, int depth)>;

// Iterative deepening ile arar: derinlik 1'den lim.max_depth'e, aspiration
// windows kullanarak. Zaman sınırı dolunca yarım kalan derinliği atar ama o
// derinlikte kökte gerçek bir iyileşme bulunduysa onu korur (abort'ta blunder
// önleme). info verilirse her tamamlanan derinlikte çağrılır.
SearchResult search_iterative(const Board& b, const SearchLimits& lim,
                              const InfoCallback& info = {});

}  // namespace engine
