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

using SquarePairTable = std::array<std::array<Bitboard, SQUARE_NB>, SQUARE_NB>;

// Sekiz kraliçe yönü (sütun/sıra/çapraz). between/line tablolarının ikisi de
// yalnız bu ışınlar üzerinde tanımlıdır; hizalı olmayan kare çiftleri 0 kalır.
constexpr int RayDf[8] = {1, 1, 1, 0, 0, -1, -1, -1};
constexpr int RayDr[8] = {1, 0, -1, 1, -1, 1, 0, -1};

// between[a][b] = a ile b ARASINDAKİ kareler (iki uç HARİÇ). Hizalı değillerse 0.
// Kullanımı: tek çekte "araya gir ya da çeken taşı al" maskesi = between | checker.
constexpr SquarePairTable compute_between() {
    SquarePairTable t{};
    for (int a = 0; a < SQUARE_NB; ++a) {
        for (int d = 0; d < 8; ++d) {
            Bitboard path = 0;  // a'dan buraya kadar geçilen kareler
            int f = (a & 7) + RayDf[d];
            int r = (a >> 3) + RayDr[d];
            while (on_board(f, r)) {
                int s   = r * 8 + f;
                t[a][s] = path;
                path |= Bitboard{1} << s;
                f += RayDf[d];
                r += RayDr[d];
            }
        }
    }
    return t;
}

// line[a][b] = a ve b'den geçen TAM ışın (her iki uç ve ötesi dahil). Hizalı
// değillerse 0. Kullanımı: pinli taş yalnız şah-pinner ışını üzerinde oynayabilir.
constexpr SquarePairTable compute_line() {
    SquarePairTable t{};
    for (int a = 0; a < SQUARE_NB; ++a) {
        // Dört eksen: yatay, dikey, iki çapraz. Her eksen için iki yön taranır.
        constexpr int axf[4] = {1, 0, 1, 1};
        constexpr int axr[4] = {0, 1, 1, -1};
        for (int ax = 0; ax < 4; ++ax) {
            Bitboard line = Bitboard{1} << a;
            for (int sign = -1; sign <= 1; sign += 2) {
                int f = (a & 7) + sign * axf[ax];
                int r = (a >> 3) + sign * axr[ax];
                while (on_board(f, r)) {
                    line |= Bitboard{1} << (r * 8 + f);
                    f += sign * axf[ax];
                    r += sign * axr[ax];
                }
            }
            // Işın üzerindeki her kare için (a'nın kendisi hariç) tabloya yaz.
            for (int sign = -1; sign <= 1; sign += 2) {
                int f = (a & 7) + sign * axf[ax];
                int r = (a >> 3) + sign * axr[ax];
                while (on_board(f, r)) {
                    t[a][r * 8 + f] = line;
                    f += sign * axf[ax];
                    r += sign * axr[ax];
                }
            }
        }
    }
    return t;
}

}  // namespace detail

// Derleme zamanı saldırı tabloları.
inline constexpr auto KnightAttacks = detail::compute_knight();
inline constexpr auto KingAttacks   = detail::compute_king();
inline constexpr auto PawnAttacks   = detail::compute_pawn();

// Kare çifti tabloları (32 KB'er). Pin/çek maskeleri için.
inline constexpr auto BetweenBB = detail::compute_between();
inline constexpr auto LineBB    = detail::compute_line();

// Erişim yardımcıları.
constexpr Bitboard knight_attacks(Square sq) { return KnightAttacks[sq]; }
constexpr Bitboard king_attacks(Square sq)   { return KingAttacks[sq]; }
constexpr Bitboard pawn_attacks(Color c, Square sq) { return PawnAttacks[c][sq]; }
constexpr Bitboard between_bb(Square a, Square b) { return BetweenBB[a][b]; }
constexpr Bitboard line_bb(Square a, Square b)    { return LineBB[a][b]; }

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
