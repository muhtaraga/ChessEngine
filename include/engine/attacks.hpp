#pragma once

// Non-sliding taşların (at, şah, piyon) saldırı tabloları.
// Tablolar derleme zamanında (constexpr) hesaplanır — çalışma zamanı init'i
// veya statik başlatma sırası sorunu yoktur. Sliding taşlar (fil/kale/vezir)
// ayrı bir adımda magic bitboard'larla eklenecek.

#include <array>

#include "engine/types.hpp"

namespace engine {

namespace detail {

// (file, rank) tahta sınırları içinde mi?
constexpr bool on_board(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

// Bir kareye, verilen (df, dr) ofset listesindeki geçerli hedeflerin
// bitboard'ını üretir. Tahta kenarındaki taşmalar (wrap-around) sınır
// kontrolüyle engellenir.
template <std::size_t N>
constexpr Bitboard step_attacks(int sq, const int (&df)[N], const int (&dr)[N]) {
    int f = sq & 7;
    int r = sq >> 3;
    Bitboard bb = 0;
    for (std::size_t i = 0; i < N; ++i) {
        int nf = f + df[i];
        int nr = r + dr[i];
        if (on_board(nf, nr))
            bb |= Bitboard{1} << (nr * 8 + nf);
    }
    return bb;
}

constexpr std::array<Bitboard, SQUARE_NB> compute_knight() {
    constexpr int df[8] = {1, 1, -1, -1, 2, 2, -2, -2};
    constexpr int dr[8] = {2, -2, 2, -2, 1, -1, 1, -1};
    std::array<Bitboard, SQUARE_NB> table{};
    for (int sq = 0; sq < SQUARE_NB; ++sq)
        table[sq] = step_attacks(sq, df, dr);
    return table;
}

constexpr std::array<Bitboard, SQUARE_NB> compute_king() {
    constexpr int df[8] = {1, 1, 1, 0, 0, -1, -1, -1};
    constexpr int dr[8] = {1, 0, -1, 1, -1, 1, 0, -1};
    std::array<Bitboard, SQUARE_NB> table{};
    for (int sq = 0; sq < SQUARE_NB; ++sq)
        table[sq] = step_attacks(sq, df, dr);
    return table;
}

constexpr std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> compute_pawn() {
    std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> table{};
    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        // Beyaz piyon yukarı (rank +1), siyah aşağı (rank -1) çapraz vurur.
        const int wdf[2] = {-1, 1};
        const int wdr[2] = {1, 1};
        const int bdf[2] = {-1, 1};
        const int bdr[2] = {-1, -1};
        table[WHITE][sq] = step_attacks(sq, wdf, wdr);
        table[BLACK][sq] = step_attacks(sq, bdf, bdr);
    }
    return table;
}

}  // namespace detail

// Derleme zamanı saldırı tabloları.
inline constexpr auto KnightAttacks = detail::compute_knight();
inline constexpr auto KingAttacks   = detail::compute_king();
inline constexpr auto PawnAttacks   = detail::compute_pawn();

// Erişim yardımcıları.
constexpr Bitboard knight_attacks(Square sq) { return KnightAttacks[sq]; }
constexpr Bitboard king_attacks(Square sq)   { return KingAttacks[sq]; }
constexpr Bitboard pawn_attacks(Color c, Square sq) { return PawnAttacks[c][sq]; }

// --- Sliding taşlar (magic bitboards) ---
//
// Magic tablolar çalışma zamanında bir kez hesaplanır. init_sliding_attacks()
// herhangi bir sliding erişiminden ÖNCE çağrılmalıdır (idempotent). attacks.cpp
// içinde statik başlatma ile de otomatik çağrılır, ama main/testlerde açıkça
// çağırmak güvenlidir.
void init_sliding_attacks();

// occ = tahtadaki tüm taşların doluluğu (blocker'lar). Dönen bitboard, ilk
// blocker'ları (yenebilecek kareleri) içerir.
Bitboard rook_attacks(Square sq, Bitboard occ);
Bitboard bishop_attacks(Square sq, Bitboard occ);

inline Bitboard queen_attacks(Square sq, Bitboard occ) {
    return rook_attacks(sq, occ) | bishop_attacks(sq, occ);
}

// Yavaş referans üreteçleri (ray-tracing). Magic tablolarını doldurmak ve
// testlerde doğrulama oracle'ı olarak kullanılır.
Bitboard slow_rook_attacks(Square sq, Bitboard occ);
Bitboard slow_bishop_attacks(Square sq, Bitboard occ);

}  // namespace engine
