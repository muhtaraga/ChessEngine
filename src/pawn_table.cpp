// Pawn hash table implementasyonu (bkz. pawn_table.hpp).

#include "engine/pawn_table.hpp"

namespace engine {

PawnTable PAWN_TABLE;

bool PawnTable::probe(std::uint64_t pawn_key, int& mg, int& eg,
                      std::uint64_t& passed_w, std::uint64_t& passed_b) const {
    const PawnBucket& slot = table_[pawn_key & kMask];
    // Önce veri sözcüklerini, EN SON key'i oku (store ters sırayla yazar). Yırtık
    // okumada (k ^ d ^ pw ^ pb) != pawn_key -> miss.
    std::uint64_t d  = slot.data.load(std::memory_order_relaxed);
    std::uint64_t pw = slot.passed_w.load(std::memory_order_relaxed);
    std::uint64_t pb = slot.passed_b.load(std::memory_order_relaxed);
    std::uint64_t k  = slot.key.load(std::memory_order_relaxed);
    if ((k ^ d ^ pw ^ pb) != pawn_key) return false;
    unpack(d, mg, eg);
    passed_w = pw;
    passed_b = pb;
    return true;
}

void PawnTable::store(std::uint64_t pawn_key, int mg, int eg,
                      std::uint64_t passed_w, std::uint64_t passed_b) {
    PawnBucket& slot = table_[pawn_key & kMask];
    std::uint64_t d = pack(mg, eg);
    // Veri sözcüklerini önce, key'i EN SON yaz (probe ters sırayla okur -> yırtık
    // okuma key doğrulamasında yakalanır).
    slot.data.store(d, std::memory_order_relaxed);
    slot.passed_w.store(passed_w, std::memory_order_relaxed);
    slot.passed_b.store(passed_b, std::memory_order_relaxed);
    slot.key.store(pawn_key ^ d ^ passed_w ^ passed_b, std::memory_order_relaxed);
}

void PawnTable::clear() {
    for (std::size_t i = 0; i < kCount; ++i) {
        table_[i].key.store(0, std::memory_order_relaxed);
        table_[i].data.store(0, std::memory_order_relaxed);
        table_[i].passed_w.store(0, std::memory_order_relaxed);
        table_[i].passed_b.store(0, std::memory_order_relaxed);
    }
}

}  // namespace engine
