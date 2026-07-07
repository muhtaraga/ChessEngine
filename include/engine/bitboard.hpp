#pragma once

// Bitboard yardımcıları: kare <-> bit dönüşümü ve temel bit işlemleri.
// Bu adımda küçük tutuluyor. lsb/msb/popcount gibi MSVC intrinsic
// sarmalayıcıları (_BitScanForward64, __popcnt64) move generation adımında
// eklenecek — burada henüz gerekmiyor.

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

}  // namespace engine
