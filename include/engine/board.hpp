#pragma once

// Board: bitboard tabanlı tahta temsili.
// Taşları hem türe göre (pieces[]) hem renge göre (colors[]) ayrı bitboard'larda
// tutan Stockfish tarzı çift temsil. Bir karedeki taş = ilgili renk ve tür
// bitboard'larının kesişimi. Bu yapı ileride NNUE feature çıkarımı için de uygun.

#include <string>

#include "engine/types.hpp"

namespace engine {

struct Board {
    Bitboard pieces[PIECE_TYPE_NB]{};  // taş türüne göre (renk bağımsız)
    Bitboard colors[COLOR_NB]{};       // renge göre doluluk

    Color        side_to_move   = WHITE;
    Square       en_passant     = SQ_NONE;  // geçerli en passant hedef karesi
    std::uint8_t castling_rights = 0;       // CastlingRight bit maskeleri
    std::uint16_t halfmove_clock  = 0;      // 50-hamle kuralı sayacı (yarım hamle)
    std::uint16_t fullmove_number = 1;      // tam hamle sayısı (1'den başlar)

    // Tahtayı tamamen boşaltır (tüm bitboard'lar 0, varsayılan durum bilgisi).
    void clear();

    // Standart satranç başlangıç dizilimini kurar.
    void set_startpos();

    // Belirtilen kareye taş yerleştirir (renk + tür bitboard'larını günceller).
    void put_piece(Color c, PieceType pt, Square sq);

    // Her iki rengin toplam doluluğu.
    Bitboard occupancy() const { return colors[WHITE] | colors[BLACK]; }

    // Bir karedeki taşın renk ve türünü sorgular.
    // Kare boşsa false döner; doluysa true döner ve out parametrelerini doldurur.
    bool piece_at(Square sq, Color& out_color, PieceType& out_type) const;

    // Tahtayı UTF-8 satranç figürinleriyle çok satırlı bir string'e çevirir.
    std::string to_string() const;

    // to_string() çıktısını standart çıkışa basar (debug amaçlı).
    void print() const;
};

}  // namespace engine
