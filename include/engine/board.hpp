#pragma once

// Board: bitboard tabanlı tahta temsili.
// Taşları hem türe göre (pieces[]) hem renge göre (colors[]) ayrı bitboard'larda
// tutan Stockfish tarzı çift temsil. Bir karedeki taş = ilgili renk ve tür
// bitboard'larının kesişimi. Bu yapı ileride NNUE feature çıkarımı için de uygun.

#include <string>

#include "engine/move.hpp"
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

    // Pozisyonun Zobrist anahtarı. set_startpos/set_fen tam hesaplar, do_move
    // artımlı günceller. Her zaman compute_key() ile tutarlı olmalıdır.
    std::uint64_t key = 0;

    // Yalnız piyon diziliminin Zobrist alt-anahtarı (pawn hash için). key ile aynı
    // ZOBRIST.psq[c][PAWN][sq] değerlerinden XOR'lanır -> key'in gerçek alt-kümesi.
    // Piyon-DIŞI durum (side/rok/ep) DAHİL DEĞİL: piyon yapısı onlardan bağımsız
    // (aynı piyonlar, farklı stm/rok/ep -> aynı pawn_key). put_piece/remove_piece
    // artımlı bakımlıdır; compute_pawn_key() ile tutarlı olmalıdır.
    std::uint64_t pawn_key = 0;

    // Tahtayı tamamen boşaltır (tüm bitboard'lar 0, varsayılan durum bilgisi).
    void clear();

    // Standart satranç başlangıç dizilimini kurar.
    void set_startpos();

    // Belirtilen kareye taş yerleştirir (renk + tür bitboard'larını günceller).
    void put_piece(Color c, PieceType pt, Square sq);

    // Belirtilen kareden taşı kaldırır (renk + tür bitboard'larından).
    void remove_piece(Color c, PieceType pt, Square sq);

    // Her iki rengin toplam doluluğu.
    Bitboard occupancy() const { return colors[WHITE] | colors[BLACK]; }

    // Verilen rengin piyon (ve şah) dışında en az bir taşı (at/fil/kale/vezir)
    // var mı? Null move pruning'de zugzwang koruması: yalnız şah+piyon kalınca
    // (bu false döner) bedava hamle vermek yanıltıcı olur, null uygulanmaz.
    bool has_non_pawn_material(Color c) const {
        return (colors[c] & ~pieces[PAWN] & ~pieces[KING]) != 0;
    }

    // Bir karedeki taşın türü (kare dolu varsayılır; boşsa PIECE_TYPE_NB).
    PieceType type_on(Square sq) const;

    // Bir karedeki taşın renk ve türünü sorgular.
    // Kare boşsa false döner; doluysa true döner ve out parametrelerini doldurur.
    bool piece_at(Square sq, Color& out_color, PieceType& out_type) const;

    // Verilen rengin şahının bulunduğu kare.
    Square king_square(Color c) const;

    // Bir hamleyi tahtaya uygular (legal olduğu varsayılır). Durum bilgisini
    // (rok hakları, en passant, sayaçlar, sıra) günceller. Perft copy-make
    // yaklaşımıyla çalışır: geri alma yok, çağıran gerekirse kopya tutar.
    void do_move(Move m);

    // Null (boş) hamle uygular: taş oynatmadan yalnızca sırayı karşı tarafa verir.
    // Null move pruning'de kullanılır (rakibe "bedava hamle"). En passant hakkını
    // düşürür (pass sonrası ep hedefi geçersiz), sırayı çevirir, 50-hamle sayacını
    // artırır. Zobrist anahtarını artımlı günceller (ep + side). do_move gibi
    // copy-make: geri alma yok, çağıran gerekirse kopya tutar.
    void make_null_move();

    // Pozisyonun Zobrist anahtarını taş dizilimi + durum bilgisinden sıfırdan
    // hesaplar. do_move'un artımlı güncellemesini doğrulamak için de kullanılır
    // (invariant: key == compute_key()).
    std::uint64_t compute_key() const;

    // pawn_key'i piyon diziliminden sıfırdan hesaplar (compute_key'in piyona
    // kısıtlanmış aynası). Artımlı bakımı doğrulamak için de kullanılır
    // (invariant: pawn_key == compute_pawn_key()).
    std::uint64_t compute_pawn_key() const;

    // FEN dizesinden pozisyon kurar. Başarılıysa true.
    bool set_fen(const std::string& fen);

    // Pozisyonu FEN dizesine çevirir (set_fen'in tersi). Round-trip garantisi:
    // set_fen(to_fen()) aynı pozisyonu üretmeli. Açılış kitabı üretimi ve
    // ileride NNUE self-play veri toplama için de kullanılır.
    std::string to_fen() const;

    // Tahtayı UTF-8 satranç figürinleriyle çok satırlı bir string'e çevirir.
    std::string to_string() const;

    // to_string() çıktısını standart çıkışa basar (debug amaçlı).
    void print() const;
};

}  // namespace engine
