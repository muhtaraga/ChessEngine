#pragma once

// Temel tipler: satranç motorunun her yerinde kullanılan enum ve typedef'ler.
// Kare eşlemesi LERF (Little-Endian Rank-File): A1=0, B1=1, ..., H8=63.
// Bu Stockfish ve Chess Programming Wiki ile uyumlu; ileride magic bitboard
// referans değerlerini doğrudan kullanmamızı sağlar.

#include <cstdint>

namespace engine {

// 64 karelik satranç tahtasını temsil eden bit kümesi.
// Her bit bir kareye karşılık gelir (bit index = kare index).
using Bitboard = std::uint64_t;

// Taş renkleri. COLOR_NB dizi boyutlandırma için kullanılır.
enum Color : int {
    WHITE,
    BLACK,
    COLOR_NB = 2
};

// Taş türleri (renk bağımsız). PIECE_TYPE_NB dizi boyutlandırma içindir.
enum PieceType : int {
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    PIECE_TYPE_NB = 6
};

// Kareler, LERF düzeninde. Alt satır (1. sıra) beyazın tarafıdır.
// sq = rank * 8 + file  (rank 0 = 1. sıra, file 0 = a sütunu).
enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    SQ_NONE = 64,
    SQUARE_NB = 64
};

// Rok hakları için bit maskeleri (castling_rights alanında kullanılır).
enum CastlingRight : std::uint8_t {
    WHITE_OO  = 1,  // beyaz kısa rok (şah kanadı)
    WHITE_OOO = 2,  // beyaz uzun rok (vezir kanadı)
    BLACK_OO  = 4,  // siyah kısa rok
    BLACK_OOO = 8   // siyah uzun rok
};

// Bir karenin sütununu (file, 0-7 => a-h) döndürür.
constexpr int file_of(Square sq) { return static_cast<int>(sq) & 7; }

// Bir karenin sırasını (rank, 0-7 => 1-8) döndürür.
constexpr int rank_of(Square sq) { return static_cast<int>(sq) >> 3; }

// file ve rank'ten kare oluşturur.
constexpr Square make_square(int file, int rank) {
    return static_cast<Square>(rank * 8 + file);
}

}  // namespace engine
