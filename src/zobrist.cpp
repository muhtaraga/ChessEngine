// Zobrist anahtar tablolarının deterministik doldurulması.

#include "engine/zobrist.hpp"

#include <cstdint>

namespace engine {

namespace {

// splitmix64: hızlı, kaliteli 64-bit PRNG. Sabit tohumla deterministik dizi
// üretir (Zobrist anahtarları için fazlasıyla yeterli dağılım).
struct SplitMix64 {
    std::uint64_t state;
    explicit SplitMix64(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};

}  // namespace

Zobrist::Zobrist() {
    // Sabit tohum: aynı pozisyon her çalıştırmada aynı anahtarı verir.
    SplitMix64 rng(0xC0FFEE123456789AULL);

    for (int c = 0; c < COLOR_NB; ++c)
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
            for (int sq = 0; sq < SQUARE_NB; ++sq)
                psq[c][pt][sq] = rng.next();

    for (int i = 0; i < 16; ++i)
        castling[i] = rng.next();

    for (int f = 0; f < 8; ++f)
        en_passant[f] = rng.next();

    side = rng.next();
}

// Tek küresel örnek. Bu TU'daki diğer statiklerden sonra sıralama sorunu yok;
// Board nesneleri yalnızca çalışma zamanında (main/uci/testler) oluşturuluyor.
const Zobrist ZOBRIST;

}  // namespace engine
