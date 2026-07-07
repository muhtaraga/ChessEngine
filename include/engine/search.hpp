#pragma once

// Arama (search): negamax + alpha-beta budama. Sabit derinlikte en iyi hamleyi
// ve puanını bulur. Iterative deepening ve zaman yönetimi Faz 2'de eklenecek.

#include <cstdint>
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

}  // namespace engine
