// Transposition table implementasyonu.

#include "engine/tt.hpp"

#include <bit>

namespace engine {

void TranspositionTable::resize(std::size_t mb) {
    if (mb < 1)
        mb = 1;

    // İstenen bayt bütçesine sığan en büyük 2'nin kuvveti kadar giriş.
    std::size_t bytes   = mb * 1024 * 1024;
    std::size_t entries = bytes / sizeof(TTEntry);
    if (entries < 1)
        entries = 1;

    // 2'nin kuvvetine aşağı yuvarla (index'te ucuz maskeleme için).
    std::size_t pow2 = std::size_t{1} << (std::bit_width(entries) - 1);

    table_.assign(pow2, TTEntry{});
    mask_       = pow2 - 1;
    generation_ = 0;
}

void TranspositionTable::clear() {
    // assign ile tüm girişleri varsayılana döndür (key=0, bound=NONE).
    table_.assign(table_.size(), TTEntry{});
    generation_ = 0;
}

bool TranspositionTable::probe(std::uint64_t key, TTEntry& out) const {
    if (table_.empty())
        return false;

    const TTEntry& e = table_[key & mask_];
    if (e.bound() != Bound::NONE && e.key == key) {
        out = e;
        return true;
    }
    return false;
}

void TranspositionTable::store(std::uint64_t key, int depth, int score,
                              Bound bound, Move move, int eval) {
    if (table_.empty())
        return;

    if (depth > kMaxTtDepth)
        depth = kMaxTtDepth;  // int8 alan sarmasın

    TTEntry& e = table_[key & mask_];

    // Değiştirme politikası: boş yuvayı, aynı pozisyonu, eski nesilden bir girişi,
    // ya da en az bizim kadar derin aranmışı (depth-preferred) üzerine yaz.
    // Aksi halde (farklı pozisyon, daha sığ, aynı nesil) mevcut derin girişi koru.
    const bool replace = e.bound() == Bound::NONE || e.key == key ||
                         e.generation() != generation_ || depth >= e.depth;
    if (!replace)
        return;

    // Aynı pozisyona ait bir girişin üstüne yazarken elimizdekinden daha zengin
    // olan alanları kaybetmeyelim: yeni hamle/eval boşsa eskisini koru.
    Move best_move = move;
    if (best_move == Move() && e.key == key)
        best_move = e.move;

    std::int16_t new_eval = static_cast<std::int16_t>(eval);
    if (new_eval == kEvalNone && e.key == key)
        new_eval = e.eval;

    e.key       = key;
    e.score     = static_cast<std::int16_t>(score);
    e.eval      = new_eval;
    e.move      = best_move;
    e.depth     = static_cast<std::int8_t>(depth);
    e.gen_bound = static_cast<std::uint8_t>((generation_ << 2) |
                                            static_cast<std::uint8_t>(bound));
}

TranspositionTable TT;

}  // namespace engine
