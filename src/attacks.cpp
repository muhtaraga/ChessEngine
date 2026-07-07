// Sliding taş saldırıları: magic bitboard implementasyonu.
//
// Yöntem: her kare için "ilgili occupancy maskesi" (kenar kareler hariç, çünkü
// kayan taş kenarı her durumda görür) çıkarılır. Bu maskenin tüm alt kümeleri
// (blocker kombinasyonları) için saldırı bitboard'ı yavaş ray-tracing ile
// hesaplanıp bir tabloya yazılır. Bir "magic" çarpan sayesinde
//   index = (occ & mask) * magic >> shift
// çakışmasız bir tablo indeksi verir. Magic sayılar başlatmada rastgele arama
// ile bulunur (çakışmasız ilk aday kabul edilir).

#include "engine/attacks.hpp"

#include <cstdint>
#include <random>

#include "engine/bitboard.hpp"

namespace engine {

namespace {

// Yön ofsetleri: {df, dr}.
constexpr int RookDirs[4][2]   = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

constexpr bool on_board(int f, int r) {
    return f >= 0 && f < 8 && r >= 0 && r < 8;
}

// Belirli yönlerde, blocker'a çarpana kadar (çarptığı kareyi dahil ederek)
// kayan saldırı bitboard'ı. Referans üreteç.
Bitboard ray_attack(int sq, Bitboard occ, const int dirs[4][2]) {
    Bitboard attacks = 0;
    int rk = sq >> 3, fl = sq & 7;
    for (int d = 0; d < 4; ++d) {
        int f = fl + dirs[d][0];
        int r = rk + dirs[d][1];
        while (on_board(f, r)) {
            int s = r * 8 + f;
            attacks |= Bitboard{1} << s;
            if (occ & (Bitboard{1} << s))  // blocker'a çarptık, dur
                break;
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return attacks;
}

// İlgili occupancy maskesi (kenar kareler hariç).
Bitboard rook_mask(int sq) {
    Bitboard mask = 0;
    int rk = sq >> 3, fl = sq & 7;
    for (int r = rk + 1; r <= 6; ++r) mask |= Bitboard{1} << (r * 8 + fl);
    for (int r = rk - 1; r >= 1; --r) mask |= Bitboard{1} << (r * 8 + fl);
    for (int f = fl + 1; f <= 6; ++f) mask |= Bitboard{1} << (rk * 8 + f);
    for (int f = fl - 1; f >= 1; --f) mask |= Bitboard{1} << (rk * 8 + f);
    return mask;
}

Bitboard bishop_mask(int sq) {
    Bitboard mask = 0;
    int rk = sq >> 3, fl = sq & 7;
    for (int r = rk + 1, f = fl + 1; r <= 6 && f <= 6; ++r, ++f) mask |= Bitboard{1} << (r * 8 + f);
    for (int r = rk + 1, f = fl - 1; r <= 6 && f >= 1; ++r, --f) mask |= Bitboard{1} << (r * 8 + f);
    for (int r = rk - 1, f = fl + 1; r >= 1 && f <= 6; --r, ++f) mask |= Bitboard{1} << (r * 8 + f);
    for (int r = rk - 1, f = fl - 1; r >= 1 && f >= 1; --r, --f) mask |= Bitboard{1} << (r * 8 + f);
    return mask;
}

// Bir kare için magic girdisi.
struct Magic {
    Bitboard  mask    = 0;
    Bitboard  magic   = 0;
    Bitboard* attacks = nullptr;  // ilgili tabloya işaretçi
    int       shift   = 0;
};

Magic RookMagics[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];

// Saldırı tabloları. Kale için maks 2^12=4096, fil için maks 2^9=512 girdi.
Bitboard RookTable[SQUARE_NB][4096];
Bitboard BishopTable[SQUARE_NB][512];

// Seyrek 64-bit rastgele sayı (magic adayı; az bit magic'i hızlandırır).
std::uint64_t sparse_random(std::mt19937_64& rng) {
    return rng() & rng() & rng();
}

// Tek bir kare için magic bul ve saldırı tablosunu doldur.
void init_square(int sq, bool rook, std::mt19937_64& rng) {
    Magic& m = rook ? RookMagics[sq] : BishopMagics[sq];
    const int (*dirs)[2] = rook ? RookDirs : BishopDirs;

    m.mask    = rook ? rook_mask(sq) : bishop_mask(sq);
    m.attacks = rook ? RookTable[sq] : BishopTable[sq];
    int bits  = popcount(m.mask);
    m.shift   = 64 - bits;

    // Maskenin tüm alt kümelerini (carry-rippler) ve referans saldırılarını üret.
    int count = 1 << bits;
    static Bitboard occ[4096];
    static Bitboard ref[4096];
    Bitboard subset = 0;
    for (int i = 0; i < count; ++i) {
        occ[i] = subset;
        ref[i] = ray_attack(sq, subset, dirs);
        subset = (subset - m.mask) & m.mask;  // bir sonraki alt küme
    }

    // Çakışmasız bir magic bulana kadar dene.
    static int used[4096];
    static int epoch = 0;
    while (true) {
        Bitboard candidate = sparse_random(rng);
        // Üst bitlerin yeterince yayılması bir hız sezgisidir.
        if (popcount((m.mask * candidate) & 0xFF00000000000000ULL) < 6)
            continue;

        ++epoch;
        bool ok = true;
        for (int i = 0; i < count && ok; ++i) {
            unsigned idx = static_cast<unsigned>((occ[i] * candidate) >> m.shift);
            if (used[idx] != epoch) {
                used[idx]      = epoch;
                m.attacks[idx] = ref[i];
            } else if (m.attacks[idx] != ref[i]) {
                ok = false;  // yıkıcı çakışma
            }
        }
        if (ok) {
            m.magic = candidate;
            return;
        }
    }
}

bool init_all() {
    std::mt19937_64 rng(0x1234567890abcdefULL);  // sabit tohum, tekrarlanabilir
    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        init_square(sq, /*rook=*/true, rng);
        init_square(sq, /*rook=*/false, rng);
    }
    return true;
}

}  // namespace

void init_sliding_attacks() {
    static bool done = init_all();  // yalnızca bir kez
    (void)done;
}

// TU'nun statik başlatmasında da tabloları hazırla (engine kütüphanesi linklendiğinde).
namespace {
const bool g_sliders_ready = (init_sliding_attacks(), true);
}

Bitboard rook_attacks(Square sq, Bitboard occ) {
    const Magic& m = RookMagics[sq];
    return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
}

Bitboard bishop_attacks(Square sq, Bitboard occ) {
    const Magic& m = BishopMagics[sq];
    return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
}

Bitboard slow_rook_attacks(Square sq, Bitboard occ) {
    return ray_attack(static_cast<int>(sq), occ, RookDirs);
}

Bitboard slow_bishop_attacks(Square sq, Bitboard occ) {
    return ray_attack(static_cast<int>(sq), occ, BishopDirs);
}

}  // namespace engine
