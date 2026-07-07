#pragma once

// Bitboard yardımcıları: kare <-> bit dönüşümü ve temel bit işlemleri.
// Sayma/tarama için C++20 <bit> kullanılıyor (std::popcount, std::countr_zero);
// bunlar MSVC'de doğrudan donanım intrinsic'lerine (__popcnt64, tzcnt)
// derlenir, ama taşınabilir ve daha temizdir.

#include <bit>

#include "engine/types.hpp"

namespace engine {

// Belirtilen kareye karşılık gelen tek-bitli bitboard.
constexpr Bitboard square_bb(Square sq) {
    return Bitboard{1} << static_cast<int>(sq);
}

// Bir karenin bitboard'da dolu olup olmadığını sorgular.
constexpr bool test_bit(Bitboard bb, Square sq) {
    return (bb & square_bb(sq)) != 0;
}

// Bir kareyi set eder (ilgili biti 1 yapar).
constexpr void set_bit(Bitboard& bb, Square sq) {
    bb |= square_bb(sq);
}

// Bir kareyi temizler (ilgili biti 0 yapar).
constexpr void clear_bit(Bitboard& bb, Square sq) {
    bb &= ~square_bb(sq);
}

// Bitboard'daki set bit (dolu kare) sayısı.
constexpr int popcount(Bitboard bb) {
    return std::popcount(bb);
}

// En düşük anlamlı set bit'in karesi (LSB). bb sıfır OLMAMALI.
constexpr Square lsb(Bitboard bb) {
    return static_cast<Square>(std::countr_zero(bb));
}

// En düşük anlamlı set bit'i çıkarıp o kareyi döndürür (bitboard iterasyonu).
// bb sıfır OLMAMALI.
constexpr Square pop_lsb(Bitboard& bb) {
    Square s = lsb(bb);
    bb &= bb - 1;  // en düşük set bit'i temizler
    return s;
}

}  // namespace engine
