#pragma once

// Statik değerlendirme (evaluation): materyal + piece-square table.
// Puan santipiyon (centipawn) cinsinden ve HAMLE SIRASI olan tarafın
// bakış açısından döner (pozitif = sıradaki taraf için iyi).
//
// PST değerleri Michniewski "Simplified Evaluation Function" (Chess
// Programming Wiki) tablolarından. Ham tablolar görsel düzende (8. sıra üstte,
// a-h soldan sağa) yazılmıştır; LERF kare indeksine derleme zamanında çevrilir.

#include <array>

#include "engine/types.hpp"

namespace engine {

struct Board;  // board.hpp'de struct olarak tanımlı — MSVC isim düzenlemesi
               // için class/struct anahtarı eşleşmeli.

// Taş başına materyal değeri (santipiyon). Şah 0 (her iki tarafta da var).
inline constexpr int MaterialValue[PIECE_TYPE_NB] = {
    100,  // PAWN
    320,  // KNIGHT
    330,  // BISHOP
    500,  // ROOK
    900,  // QUEEN
    0     // KING
};

namespace detail {

// Ham PST tabloları, görsel düzen: indeks 0 = a8, indeks 63 = h1.
inline constexpr int RawPST[PIECE_TYPE_NB][SQUARE_NB] = {
    // PAWN
    {  0,  0,  0,  0,  0,  0,  0,  0,
      50, 50, 50, 50, 50, 50, 50, 50,
      10, 10, 20, 30, 30, 20, 10, 10,
       5,  5, 10, 25, 25, 10,  5,  5,
       0,  0,  0, 20, 20,  0,  0,  0,
       5, -5,-10,  0,  0,-10, -5,  5,
       5, 10, 10,-20,-20, 10, 10,  5,
       0,  0,  0,  0,  0,  0,  0,  0 },
    // KNIGHT
    { -50,-40,-30,-30,-30,-30,-40,-50,
      -40,-20,  0,  0,  0,  0,-20,-40,
      -30,  0, 10, 15, 15, 10,  0,-30,
      -30,  5, 15, 20, 20, 15,  5,-30,
      -30,  0, 15, 20, 20, 15,  0,-30,
      -30,  5, 10, 15, 15, 10,  5,-30,
      -40,-20,  0,  5,  5,  0,-20,-40,
      -50,-40,-30,-30,-30,-30,-40,-50 },
    // BISHOP
    { -20,-10,-10,-10,-10,-10,-10,-20,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5, 10, 10,  5,  0,-10,
      -10,  5,  5, 10, 10,  5,  5,-10,
      -10,  0, 10, 10, 10, 10,  0,-10,
      -10, 10, 10, 10, 10, 10, 10,-10,
      -10,  5,  0,  0,  0,  0,  5,-10,
      -20,-10,-10,-10,-10,-10,-10,-20 },
    // ROOK
    {  0,  0,  0,  0,  0,  0,  0,  0,
       5, 10, 10, 10, 10, 10, 10,  5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
       0,  0,  0,  5,  5,  0,  0,  0 },
    // QUEEN
    { -20,-10,-10, -5, -5,-10,-10,-20,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5,  5,  5,  5,  0,-10,
       -5,  0,  5,  5,  5,  5,  0, -5,
        0,  0,  5,  5,  5,  5,  0, -5,
      -10,  5,  5,  5,  5,  5,  0,-10,
      -10,  0,  5,  0,  0,  0,  0,-10,
      -20,-10,-10, -5, -5,-10,-10,-20 },
    // KING (orta oyun)
    { -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -20,-30,-30,-40,-40,-30,-30,-20,
      -10,-20,-20,-20,-20,-20,-20,-10,
       20, 20,  0,  0,  0,  0, 20, 20,
       20, 30, 10,  0,  0, 10, 30, 20 }
};

// Beyaz için LERF-indeksli PST: white_sq (a1=0) -> görsel indeks.
constexpr std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> make_pst() {
    std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> t{};
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        for (int sq = 0; sq < SQUARE_NB; ++sq) {
            int rank = sq >> 3, file = sq & 7;
            int vis  = (7 - rank) * 8 + file;  // LERF -> görsel
            t[pt][sq] = RawPST[pt][vis];
        }
    }
    return t;
}

}  // namespace detail

// Beyaz bakışıyla PST. Siyah için dikey ayna: PST[pt][sq ^ 56].
inline constexpr auto PST = detail::make_pst();

// Hamle sırası olan tarafın bakışından statik değerlendirme (santipiyon).
int evaluate(const Board& b);

}  // namespace engine
