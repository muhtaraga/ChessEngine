// Pawn hash table implementasyonu (bkz. pawn_table.hpp).

#include "engine/pawn_table.hpp"

namespace engine {

PawnTable PAWN_TABLE;

bool PawnTable::probe(std::uint64_t pawn_key, int& mg, int& eg) const {
    const PawnBucket& slot = table_[pawn_key & kMask];
    // Önce data, sonra key oku (store ters sırayla yazar). Yırtık okumada
    // (k ^ d) != pawn_key -> miss.
    std::uint64_t d = slot.data.load(std::memory_order_relaxed);
    std::uint64_t k = slot.key.load(std::memory_order_relaxed);
    if ((k ^ d) != pawn_key) return false;
    unpack(d, mg, eg);
    return true;
}

void PawnTable::store(std::uint64_t pawn_key, int mg, int eg) {
    PawnBucket& slot = table_[pawn_key & kMask];
    std::uint64_t d = pack(mg, eg);
    // data'yı önce, key'i sonra yaz (probe ters sırayla okur -> yırtık okuma
    // key doğrulamasında yakalanır).
    slot.data.store(d, std::memory_order_relaxed);
    slot.key.store(pawn_key ^ d, std::memory_order_relaxed);
}

void PawnTable::clear() {
    for (std::size_t i = 0; i < kCount; ++i) {
        table_[i].key.store(0, std::memory_order_relaxed);
        table_[i].data.store(0, std::memory_order_relaxed);
    }
}

}  // namespace engine
