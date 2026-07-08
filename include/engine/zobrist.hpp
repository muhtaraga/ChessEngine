#pragma once

// Zobrist hashing: bir pozisyonu (yaklaşık) tek bir 64-bit anahtara indirger.
// Her (renk, tür, kare) üçlüsü, her rok hakkı kombinasyonu, en passant sütunu ve
// "sıra siyahta" durumu için rastgele bir anahtar tutulur; pozisyonun anahtarı bu
// bileşenlerin XOR'udur. XOR tersine çevrilebilir olduğundan anahtar do_move
// içinde artımlı (incremental) güncellenebilir — transposition table'ın temeli.
//
// Anahtarlar sabit tohumlu (deterministik) bir PRNG ile doldurulur; böylece aynı
// pozisyon her çalıştırmada aynı anahtarı üretir (TT dosyaya yazılırsa, debug
// tekrarlanabilirliği vb. için önemli).

#include "engine/types.hpp"

namespace engine {

struct Zobrist {
    // [renk][tür][kare] taş-kare anahtarları.
    Bitboard psq[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];
    // Rok hakları maskesine (0..15) göre anahtar. Tüm mask için ayrı giriş
    // tutmak, do_move'da hakları tek XOR ile güncellemeyi sağlar.
    Bitboard castling[16];
    // En passant hedef karesinin sütununa (0..7) göre anahtar.
    Bitboard en_passant[8];
    // Sıra siyahtayken XOR'lanan anahtar (beyazda XOR'lanmaz).
    Bitboard side;

    // Sabit tohumlu splitmix64 ile tüm tabloları doldurur.
    Zobrist();
};

// Program genelinde tek, sabit anahtar kümesi (static init sırasında dolar).
extern const Zobrist ZOBRIST;

}  // namespace engine
